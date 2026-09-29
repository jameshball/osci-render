#pragma once

#include "../model/Document.h"
#include "../model/ModulationGraph.h"
#include "PreparedEffects.h"
#include <functional>
#include <map>
#include <tuple>

namespace motion {
// Attaches routed modulators and resolved property links to prepared curve
// copies. Preparation-thread only; the attached drivers are immutable.
class PreparedDrivers {
public:
    using Loudness = std::function<std::shared_ptr<const SoundtrackEnvelope>()>;
    explicit PreparedDrivers(Loudness loudness) : loudness(std::move(loudness)) {}

    // `scopeClock` maps project seconds to the scope's own seconds.
    void drive(Curve& curve, const Composition& scope, const ClipTiming& scopeClock, Id owner, const std::string& property) {
        drive(curve, scope, scopeClock, owner, property, 0);
    }
    void driveEffects(std::vector<PreparedEffect>& prepared, const std::vector<EffectInstance>& effects, const Composition& scope, const ClipTiming& scopeClock) {
        for (std::size_t index = 0; index < prepared.size() && index < effects.size(); ++index) {
            const auto* definition = effectDefinition(effects[index].type);
            if (definition == nullptr) { continue; }
            for (std::size_t parameter = 0; parameter < prepared[index].count && parameter < definition->parameters.size(); ++parameter) {
                drive(prepared[index].curves[parameter], scope, scopeClock, effects[index].id, definition->parameters[parameter].id);
            }
        }
    }

private:
    using ScopeKey = std::tuple<const Composition*, double, double, double>;
    struct ScopeState {
        std::map<ModulatorId, std::shared_ptr<const PreparedModulator>> modulators;
        std::map<std::pair<Id, std::string>, std::shared_ptr<const Curve>> sources;
    };
    static constexpr int maximumLinkDepth = 64;

    ScopeState& state(const Composition& scope, const ClipTiming& clock) {
        return scopes[ScopeKey {&scope, clock.start, clock.offset, clock.rate}];
    }

    void drive(Curve& curve, const Composition& scope, const ClipTiming& scopeClock, Id owner, const std::string& property, int depth) {
        const auto target = findPropertyTarget(scope, owner);
        if (!target.has_value()) { return; }
        auto drivers = std::make_shared<CurveDrivers>();
        drivers->start = target->start;
        drivers->offset = target->offset;
        drivers->rate = target->rate;
        drivers->projectStart = scopeClock.start;
        drivers->projectOffset = scopeClock.offset;
        drivers->projectRate = scopeClock.rate;
        for (const auto& route : scope.routes) {
            if (route.target != owner || route.property != property) { continue; }
            auto modulator = prepare(scope, scopeClock, route.modulator);
            if (modulator != nullptr) { drivers->routes.push_back({std::move(modulator), route.amount, route.mode}); }
        }
        if (curve.link.has_value() && depth < maximumLinkDepth) {
            const auto source = findPropertyTarget(scope, curve.link->source);
            auto sourceCurve = source.has_value() ? linkedSource(scope, scopeClock, curve.link->source, curve.link->property, depth + 1) : nullptr;
            if (sourceCurve != nullptr) {
                drivers->link = *curve.link;
                drivers->linkSource = std::move(sourceCurve);
                drivers->linkStart = source->start;
                drivers->linkOffset = source->offset;
                drivers->linkRate = source->rate;
                drivers->linkBpm = source->curveBpm(scope.bpm);
            }
        }
        if (drivers->routes.empty() && drivers->linkSource == nullptr) { return; }
        curve.drivers = std::move(drivers);
    }

    std::shared_ptr<const Curve> linkedSource(const Composition& scope, const ClipTiming& scopeClock, Id owner, const std::string& property, int depth) {
        auto& cache = state(scope, scopeClock).sources;
        const auto key = std::make_pair(owner, property);
        const auto found = cache.find(key);
        if (found != cache.end()) { return found->second; }
        const auto* authored = findPropertyCurve(scope, owner, property);
        if (authored == nullptr) { return nullptr; }
        auto copy = std::make_shared<Curve>(*authored);
        copy->drivers.reset();
        drive(*copy, scope, scopeClock, owner, property, depth);
        attachLoudness(*copy, scope, scopeClock, owner);
        std::shared_ptr<const Curve> result = std::move(copy);
        state(scope, scopeClock).sources[key] = result;
        return result;
    }

    // A link source is a private copy the soundtrack pass never visits.
    void attachLoudness(Curve& curve, const Composition& scope, const ClipTiming& scopeClock, Id owner) {
        if (!curve.modulation.enabled || curve.modulation.waveform != ModulationWaveform::soundtrack) { return; }
        const auto envelope = loudness ? loudness() : nullptr;
        const auto target = findPropertyTarget(scope, owner);
        if (envelope == nullptr || !target.has_value() || scopeClock.rate == 0) { return; }
        // local -> composition -> project, composed into one affine clock.
        const auto start = scopeClock.start + (target->start - scopeClock.offset) / scopeClock.rate;
        curve.modulation.soundtrack = std::make_shared<const SoundtrackClock>(SoundtrackClock {envelope, start, target->offset, target->rate * scopeClock.rate});
    }

    std::shared_ptr<const PreparedModulator> prepare(const Composition& scope, const ClipTiming& scopeClock, ModulatorId id) {
        auto& cache = state(scope, scopeClock).modulators;
        const auto found = cache.find(id);
        if (found != cache.end()) { return found->second; }
        const auto modulator = std::find_if(scope.modulators.begin(), scope.modulators.end(), [id](const auto& item) { return item.id == id; });
        if (modulator == scope.modulators.end() || !modulator->valid()) { return nullptr; }
        auto prepared = std::make_shared<PreparedModulator>();
        prepared->kind = modulator->kind;
        prepared->shape = modulator->shape;
        prepared->shape.enabled = true;
        prepared->shape.amount = 1;
        prepared->shape.mode = ModulationMode::add;
        prepared->bpm = scope.bpm;
        if (modulator->kind == ModulatorKind::oscillator && modulator->shape.waveform == ModulationWaveform::soundtrack && loudness) {
            prepared->soundtrack = loudness();
        }
        if (modulator->kind == ModulatorKind::envelope) {
            prepared->attack = modulator->attack;
            prepared->decay = modulator->decay;
            prepared->sustain = modulator->sustain;
            prepared->release = modulator->release;
            collectNotes(*prepared, *modulator, scope);
        }
        std::shared_ptr<const PreparedModulator> result = std::move(prepared);
        cache.emplace(id, result);
        return result;
    }

    static void collectNotes(PreparedModulator& prepared, const Modulator& modulator, const Composition& scope) {
        for (const auto& track : scope.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != modulator.source || clip.midi == nullptr) { continue; }
                const auto timing = clip.timing(scope.tempo());
                if (!timing.valid()) { return; }
                const auto secondsPerBeat = 60 / clip.curveBpm(scope.tempo());
                const auto resolve = [&](double beat) { return timing.start + (beat * secondsPerBeat - timing.offset) / timing.rate; };
                for (const auto& note : clip.midi->notes()) {
                    if (note.pitch < modulator.lowestPitch || note.pitch > modulator.highestPitch) { continue; }
                    const auto start = std::max(timing.start, resolve(note.start));
                    const auto end = std::min(timing.end(), resolve(note.end()));
                    if (!(end > start)) { continue; }
                    const auto level = (1 - modulator.velocity) + modulator.velocity * note.velocity / 127.0;
                    prepared.notes.push_back({start, end, level});
                }
                std::sort(prepared.notes.begin(), prepared.notes.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
                prepared.buildIndex();
                return;
            }
        }
    }

    Loudness loudness;
    std::map<ScopeKey, ScopeState> scopes;
};
}
