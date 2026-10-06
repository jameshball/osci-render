#pragma once

#include "PropertyTarget.h"
#include <functional>
#include <map>
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
    for (std::size_t index = 0; index < composition.tracks.size(); ++index) {
        auto& track = itemAt(composition.tracks, index);
        effects(track.effects);
        for (auto& clip : track.clips) { visit(clip.id, clip.properties); effects(clip.effects); }
    }
    for (auto& camera : composition.cameras) { visit(camera.id, camera.properties); }
    if constexpr (requires { composition.beam; }) { visit(composition.beam.id, composition.beam.properties); }
}

template <typename CompositionType>
bool hasPropertyCurve(const CompositionType& composition, Id target, const std::string& property) {
    return findPropertyCurve(composition, target, property) != nullptr;
}

// Visual properties (Lua sliders included: their bakes follow them) can be
// driven by routes and links; audio clip gain and pan use their curves only.
template <typename CompositionType>
bool drivableProperty(const CompositionType& composition, Id target, const std::string& property) {
    const auto found = findPropertyTarget(composition, target);
    return found.has_value() && !found->isAudio && found->curve(property) != nullptr;
}

// The same rule for many references at once: every owner's properties by
// id, built in one pass.
class DrivableProperties {
public:
    template <typename CompositionType>
    explicit DrivableProperties(const CompositionType& composition) {
        forEachPropertyMap(composition, [this](Id owner, const PropertyMap& properties) { owners.emplace(owner, &properties); });
        for (const auto& track : composition.tracks) {
            if (track.kind != TrackKind::audio) { continue; }
            for (const auto& clip : track.clips) { owners.erase(clip.id); }
        }
    }
    bool contains(Id owner, std::string_view property) const {
        const auto found = owners.find(owner);
        return found != owners.end() && found->second->contains(property);
    }

private:
    std::map<Id, const PropertyMap*> owners;
};

// A visual media clip (the kind that can carry MIDI or be a camera target).
template <typename CompositionType>
bool hasVisualClip(const CompositionType& composition, Id clipId) {
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
    std::set<Id> modulators;
    for (const auto& modulator : composition.modulators) {
        if (!modulator.valid() || !modulators.insert(modulator.id).second) { return "Invalid or duplicate modulator settings."; }
        if (modulator.kind != ModulatorKind::oscillator && modulator.source != 0 && !hasVisualClip(composition, modulator.source)) {
            return "A MIDI modulator follows a clip that does not exist in its composition.";
        }
    }
    const DrivableProperties drivable(composition);
    for (const auto& route : composition.routes) {
        if (!route.valid() || !modulators.contains(route.modulator) || !drivable.contains(route.target, route.property)) {
            return "A modulation route references a missing modulator or property.";
        }
    }
    std::string error;
    forEachPropertyMap(composition, [&](Id owner, const auto& properties) {
        for (const auto& [name, curve] : properties) {
            if (!error.empty() || !curve.link.has_value()) { continue; }
            if (!drivable.contains(owner, name)) { error = "Audio clip gain and pan cannot be linked."; continue; }
            if (!hasPropertyCurve(composition, curve.link->source, curve.link->property)) { error = "A property link references a missing property."; continue; }
            if (linkCreatesCycle(composition, owner, name, *curve.link)) { error = "Property links must not form a cycle."; }
        }
    });
    return error;
}

// After cloning property owners (old id -> new id), gives each clone the
// routes of its original and points links between cloned owners at the
// clones, so a copy behaves like its original rather than following it.
template <typename CompositionType, typename NewId>
void cloneDrivers(CompositionType& composition, const std::map<Id, Id>& owners, NewId&& newId) {
    std::vector<ModulationRoute> added;
    for (const auto& route : composition.routes) {
        const auto owner = owners.find(route.target);
        if (owner == owners.end()) { continue; }
        auto copy = route;
        copy.id = newId();
        copy.target = owner->second;
        added.push_back(std::move(copy));
    }
    composition.routes.insert(composition.routes.end(), added.begin(), added.end());
    std::set<Id> clones;
    for (const auto& [original, clone] : owners) { clones.insert(clone); }
    forEachPropertyMap(composition, [&](Id owner, auto& properties) {
        if (!clones.contains(owner)) { return; }
        for (auto& [name, curve] : properties) {
            if (!curve.link.has_value()) { continue; }
            const auto source = owners.find(curve.link->source);
            if (source != owners.end()) { curve.link->source = source->second; }
        }
    });
}

// Drops routes, links, envelope sources and camera targets/parents whose
// referents were deleted, so removing a clip never leaves the composition
// unloadable.
template <typename CompositionType>
void pruneReferences(CompositionType& composition) {
    std::set<Id> modulators;
    for (auto& modulator : composition.modulators) {
        modulators.insert(modulator.id);
        if (modulator.kind != ModulatorKind::oscillator && modulator.source != 0 && !hasVisualClip(composition, modulator.source)) { modulator.source = 0; }
    }
    const DrivableProperties drivable(composition);
    std::erase_if(composition.routes, [&](const auto& route) {
        return !modulators.contains(route.modulator) || !drivable.contains(route.target, route.property);
    });
    std::vector<std::pair<Id, std::string>> broken;
    // Read-only walk: a mutable one would copy every shared track.
    forEachPropertyMap(std::as_const(composition), [&](Id owner, const auto& properties) {
        for (const auto& [name, curve] : properties) {
            if (curve.link.has_value() && !hasPropertyCurve(composition, curve.link->source, curve.link->property)) { broken.emplace_back(owner, name); }
        }
    });
    for (const auto& [owner, name] : broken) {
        auto* curve = findPropertyCurve(composition, owner, name);
        if (curve != nullptr) { curve->link.reset(); }
    }
    for (auto& camera : composition.cameras) {
        if (camera.parent != 0 && findGroup(composition, camera.parent) == nullptr) { camera.parent = 0; }
        if (camera.target != 0 && findGroup(composition, camera.target) == nullptr && !hasVisualClip(composition, camera.target)) { camera.target = 0; }
    }
}
}
