#pragma once

#include "PropertyTarget.h"
#include <functional>
#include <set>
#include <string>
#include <utility>

namespace motion {
// Visits every property map in one composition scope with its owner's id.
template <typename CompositionType, typename Visitor>
void forEachPropertyMap(CompositionType& composition, Visitor&& visit) {
    const auto effects = [&](auto& list) { for (auto& effect : list) { visit(effect.id, effect.properties); } };
    effects(composition.effects);
    for (auto& group : composition.groups) { visit(group.id, group.properties); effects(group.effects); }
    for (auto& track : composition.tracks) {
        effects(track.effects);
        for (auto& clip : track.clips) { visit(clip.id, clip.properties); effects(clip.effects); }
    }
    for (auto& camera : composition.cameras) { visit(camera.id, camera.properties); }
}

template <typename CompositionType>
bool hasPropertyCurve(const CompositionType& composition, Id target, const std::string& property) {
    return findPropertyCurve(composition, target, property) != nullptr;
}

template <typename CompositionType>
bool hasMidiClip(const CompositionType& composition, Id clipId) {
    for (const auto& track : composition.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == clipId) { return track.kind == TrackKind::visual && clip.composition == 0; }
        }
    }
    return false;
}

// True when following links from (target, property) with `link` in place
// would return to it, or run deeper than any sane chain.
template <typename CompositionType>
bool linkCreatesCycle(const CompositionType& composition, Id target, const std::string& property, const PropertyLink& link) {
    auto node = std::make_pair(link.source, link.property);
    for (int depth = 0; depth < 64; ++depth) {
        if (node.first == target && node.second == property) { return true; }
        const auto* curve = findPropertyCurve(composition, node.first, node.second);
        if (curve == nullptr || !curve->link.has_value()) { return false; }
        node = std::make_pair(curve->link->source, curve->link->property);
    }
    return true;
}

// Structural checks shared by loading and editing. Identity uniqueness across
// the project is checked by the loader's identity set.
template <typename CompositionType>
std::string validateModulation(const CompositionType& composition) {
    std::set<ModulatorId> modulators;
    for (const auto& modulator : composition.modulators) {
        if (!modulator.valid() || !modulators.insert(modulator.id).second) { return "Invalid or duplicate modulator settings."; }
        if (modulator.kind == ModulatorKind::envelope && modulator.source != 0 && !hasMidiClip(composition, modulator.source)) {
            return "An envelope modulator follows a clip that does not exist in its composition.";
        }
    }
    std::set<std::pair<std::uint64_t, std::string>> routed;
    for (const auto& route : composition.routes) {
        if (!route.valid() || !modulators.contains(route.modulator) || !hasPropertyCurve(composition, route.target, route.property)) {
            return "A modulation route references a missing modulator or property.";
        }
    }
    std::string error;
    forEachPropertyMap(composition, [&](Id owner, const auto& properties) {
        for (const auto& [name, curve] : properties) {
            if (!error.empty() || !curve.link.has_value()) { continue; }
            if (!hasPropertyCurve(composition, curve.link->source, curve.link->property)) { error = "A property link references a missing property."; continue; }
            if (linkCreatesCycle(composition, owner, name, *curve.link)) { error = "Property links must not form a cycle."; }
        }
    });
    return error;
}

// Drops routes, links and envelope sources whose referents were deleted, so
// removing a clip never leaves the composition unloadable.
template <typename CompositionType>
void pruneModulation(CompositionType& composition) {
    std::set<ModulatorId> modulators;
    for (auto& modulator : composition.modulators) {
        modulators.insert(modulator.id);
        if (modulator.kind == ModulatorKind::envelope && modulator.source != 0 && !hasMidiClip(composition, modulator.source)) { modulator.source = 0; }
    }
    std::erase_if(composition.routes, [&](const auto& route) {
        return !modulators.contains(route.modulator) || !hasPropertyCurve(composition, route.target, route.property);
    });
    std::vector<std::pair<Id, std::string>> broken;
    forEachPropertyMap(composition, [&](Id owner, const auto& properties) {
        for (const auto& [name, curve] : properties) {
            if (curve.link.has_value() && !hasPropertyCurve(composition, curve.link->source, curve.link->property)) { broken.emplace_back(owner, name); }
        }
    });
    for (const auto& [owner, name] : broken) {
        auto* curve = findPropertyCurve(composition, owner, name);
        if (curve != nullptr) { curve->link.reset(); }
    }
}
}
