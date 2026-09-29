#pragma once

#include "CompositionGraph.h"
#include "Group.h"
#include <atomic>

namespace motion {
// A preparation-only view. Pointers refer to the immutable document snapshot;
// consumers must copy the required curves/media into their prepared state.
// Stages are outermost first, followed by the leaf. Each scope clock maps main
// project seconds to that stage's owning composition, independently of its clip.
struct CompositionStage {
    const Track* track = nullptr;
    const Clip* clip = nullptr;
    ClipTiming scopeClock;
    ClipTiming clipClock;
    double bpm = 120;
    const std::vector<Group>* groups = nullptr;
    const std::vector<EffectInstance>* effects = nullptr;
    Tempo tempo;
};

struct CompositionExpansionResult {
    std::string error;
    std::size_t leaves = 0;
    explicit operator bool() const { return error.empty(); }
};

// Calls visitor(scope, stages) for audible media leaves. All graph validation
// happens before visiting, including dormant definitions. Callers must discard
// any prepared partial result if cancellation or invalid timing returns an error.
template <typename ProjectType, typename Visitor>
CompositionExpansionResult expandComposition(const ProjectType& project, Visitor&& visitor, const std::atomic<bool>* cancel = nullptr) {
    const auto graph = validateCompositionGraph(project);
    if (!graph) { return {graph.error, 0}; }
    CompositionExpansionResult result;
    using Definition = typename decltype(project.definitions)::value_type::element_type;
    std::map<Id, const Definition*> definitions;
    for (const auto& definition : project.definitions) { definitions.emplace(definition->id, definition.get()); }
    std::vector<CompositionStage> stages;
    stages.reserve(maximumCompositionDepth + 1);
    const auto visit = [&](auto&& self, const auto& scope, const ClipTiming& clock) -> bool {
        if (cancel != nullptr && cancel->load()) { result.error = "Composition preparation cancelled."; return false; }
        if (!std::isfinite(scope.duration) || scope.duration <= 0 || !std::isfinite(scope.bpm) || scope.bpm <= 0) {
            result.error = "Invalid reusable composition duration or tempo.";
            return false;
        }
        // Definitions stop at their duration; a longer instance leaves silence.
        const auto scopeStart = clock.start - clock.offset / clock.rate;
        const auto scopeEnd = clock.start + (scope.duration - clock.offset) / clock.rate;
        if (!std::isfinite(scopeStart) || !std::isfinite(scopeEnd)
            || (scopeEnd <= scopeStart && scopeStart >= clock.start && scopeStart < clock.end())) {
            result.error = "Composition timing exceeds the supported numeric range.";
            return false;
        }
        const auto visible = ClipTiming(0, scope.duration).nestedIn(clock);
        if (!visible.has_value()) { return true; }
        for (const auto& track : scope.tracks) {
            if (!trackIsAudible(scope, track)) { continue; }
            for (const auto& clip : track.clips) {
                if (cancel != nullptr && cancel->load()) { result.error = "Composition preparation cancelled."; return false; }
                const auto timing = clip.timing(scope.tempo());
                if (!clip.valid() || !timing.valid() || !std::isfinite(timing.rate * visible->rate)
                    || timing.rate * visible->rate <= 0) {
                    result.error = "Invalid timing while preparing a composition clip.";
                    return false;
                }
                const auto mappedStart = visible->start + (timing.start - visible->offset) / visible->rate;
                const auto mappedEnd = visible->start + (timing.end() - visible->offset) / visible->rate;
                const auto first = std::max(visible->start, mappedStart);
                const auto last = std::min(visible->end(), mappedEnd);
                if (!std::isfinite(mappedStart) || !std::isfinite(mappedEnd)
                    || (mappedEnd <= mappedStart && mappedStart >= visible->start && mappedStart < visible->end())
                    || (first < last && !std::isfinite(timing.localTime(visible->localTime(first))))) {
                    result.error = "Composition timing exceeds the supported numeric range.";
                    return false;
                }
                const auto mapped = timing.nestedIn(*visible);
                if (!mapped.has_value()) { continue; }
                stages.push_back({&track, &clip, *visible, *mapped, scope.bpm, &scope.groups, &scope.effects, scope.tempo()});
                if (clip.composition != 0) {
                    if (clip.midi != nullptr) {
                        result.error = "MIDI patterns must be assigned to media clips inside a reusable composition.";
                        return false;
                    }
                    if (track.kind != TrackKind::visual) {
                        result.error = "Reusable compositions require a visual track.";
                        return false;
                    }
                    if (!self(self, *definitions.at(clip.composition), *mapped)) { return false; }
                } else {
                    visitor(scope, stages);
                    ++result.leaves;
                }
                stages.pop_back();
            }
        }
        return true;
    };
    const ClipTiming mainClock(0, project.duration);
    if (!mainClock.valid()) { return {"Invalid main composition duration.", 0}; }
    visit(visit, project, mainClock);
    return result;
}
}
