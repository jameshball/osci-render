#pragma once

#include "Timeline.h"
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>

namespace motion {
inline constexpr std::size_t maximumCompositionDepth = 32;
inline constexpr std::size_t maximumExpandedClips = 100000;

// Visits the main composition, then each reusable definition.
template <typename ProjectType, typename Visit>
void forEachComposition(const ProjectType& project, Visit&& visit) {
    visit(project);
    for (const auto& definition : project.definitions) {
        if (definition != nullptr) { visit(*definition); }
    }
}

// Changes the main composition and each definition `needs` picks. Those
// definitions are copied first: snapshots elsewhere share them unchanged.
template <typename ProjectType, typename Needs, typename Change>
void changeEachComposition(ProjectType& project, Needs&& needs, Change&& change) {
    using Definition = typename std::remove_cvref_t<decltype(project.definitions)>::value_type::element_type;
    if (needs(std::as_const(project))) { change(project); }
    for (auto& definition : project.definitions) {
        if (definition == nullptr || !needs(std::as_const(*definition))) { continue; }
        auto copy = std::make_shared<std::remove_const_t<Definition>>(*definition);
        change(*copy);
        definition = std::move(copy);
    }
}

struct CompositionGraphResult {
    std::string error;
    std::size_t expandedClips = 0, depth = 0;
    explicit operator bool() const { return error.empty(); }
};

// Editor/preparation-thread validation. Repeated instances reuse memoised
// summaries; an exponential DAG cannot force exponential validation work.
template <typename ProjectType>
CompositionGraphResult validateCompositionGraph(const ProjectType& project) {
    CompositionGraphResult result;
    using Definition = typename decltype(project.definitions)::value_type::element_type;
    std::map<Id, const Definition*> definitions;
    for (const auto& definition : project.definitions) {
        if (definition == nullptr || definition->id == 0 || !definitions.emplace(definition->id, definition.get()).second) {
            result.error = "Invalid or duplicate reusable composition identity.";
            return result;
        }
    }
    struct Summary { std::size_t clips = 0, depth = 0; };
    std::map<Id, Summary> summaries;
    std::set<Id> visiting;
    const auto visit = [&](auto&& self, const auto& scope, std::size_t depth) -> std::optional<Summary> {
        if (depth > maximumCompositionDepth) {
            result.error = "Reusable compositions exceed 32 nesting levels.";
            return std::nullopt;
        }
        Summary summary;
        for (const auto& track : scope.tracks) {
            for (const auto& clip : track.clips) {
                std::size_t cost = 1;
                if (clip.composition != 0) {
                    if (clip.asset != 0) {
                        result.error = "A clip cannot reference both media and a composition.";
                        return std::nullopt;
                    }
                    const auto found = definitions.find(clip.composition);
                    if (found == definitions.end()) {
                        result.error = "A reusable composition reference is missing.";
                        return std::nullopt;
                    }
                    if (visiting.contains(clip.composition)) {
                        result.error = "Reusable compositions cannot contain cycles.";
                        return std::nullopt;
                    }
                    auto cached = summaries.find(clip.composition);
                    if (cached == summaries.end()) {
                        visiting.insert(clip.composition);
                        const auto child = self(self, *found->second, depth + 1);
                        visiting.erase(clip.composition);
                        if (!child.has_value()) { return std::nullopt; }
                        cached = summaries.emplace(clip.composition, *child).first;
                    }
                    const auto& child = cached->second;
                    if (depth + 1 + child.depth > maximumCompositionDepth) {
                        result.error = "Reusable compositions exceed 32 nesting levels.";
                        return std::nullopt;
                    }
                    summary.depth = std::max(summary.depth, child.depth + 1);
                    cost += child.clips;
                }
                if (cost > maximumExpandedClips - summary.clips) {
                    result.error = "Reusable composition expansion exceeds 100000 clips.";
                    return std::nullopt;
                }
                summary.clips += cost;
            }
        }
        return summary;
    };
    // Validate unused definitions as well: otherwise a dormant cycle could be
    // saved and become active on a later drag from the library.
    for (const auto& [id, definition] : definitions) {
        if (summaries.contains(id)) { continue; }
        visiting.insert(id);
        const auto summary = visit(visit, *definition, 0);
        visiting.erase(id);
        if (!summary.has_value()) { return result; }
        summaries.emplace(id, *summary);
    }
    const auto root = visit(visit, project, 0);
    if (root.has_value()) { result.expandedClips = root->clips; result.depth = root->depth; }
    return result;
}

template <typename ProjectType>
Id highestProjectIdentity(const ProjectType& project, Id highest = 0) {
    const auto effects = [&](const auto& values) { for (const auto& value : values) { highest = std::max(highest, value.id); } };
    const auto scope = [&](const auto& composition) {
        effects(composition.effects);
        for (const auto& group : composition.groups) { highest = std::max(highest, group.id); effects(group.effects); }
        for (const auto& track : composition.tracks) {
            highest = std::max(highest, track.id); effects(track.effects);
            for (const auto& clip : track.clips) { highest = std::max(highest, clip.id); effects(clip.effects); }
        }
        for (const auto& camera : composition.cameras) { highest = std::max(highest, camera.id); }
        for (const auto& marker : composition.markers) { highest = std::max(highest, marker.id); }
        for (const auto& cut : composition.cameraCuts) { highest = std::max(highest, cut.id); }
        effects(composition.modulators);
        effects(composition.routes);
    };
    scope(project);
    for (const auto& asset : project.assets) { if (asset != nullptr) { highest = std::max(highest, asset->id); } }
    for (const auto& definition : project.definitions) {
        if (definition != nullptr) { highest = std::max(highest, definition->id); scope(*definition); }
    }
    return highest;
}


template <typename ProjectType>
std::size_t sourceReferenceCount(const ProjectType& project, Id asset) {
    std::size_t count = 0;
    const auto scope = [&](const auto& composition) {
        for (const auto& track : composition.tracks) {
            for (const auto& clip : track.clips) { if (clip.asset == asset && asset != 0) { ++count; } }
        }
    };
    forEachComposition(project, scope);
    return count;
}

}
