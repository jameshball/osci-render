#pragma once

#include "Camera.h"
#include "Beam.h"
#include "Group.h"
#include "SharedList.h"
#include <string_view>
#include <type_traits>

namespace motion {

// A borrowed view, valid until its project's tracks/cameras/properties change.
// Resolve again for each mutation; neither names nor property maps are copied.
template <typename Map>
struct BasicPropertyTarget {
    Id id = 0;
    std::string_view name;
    double start = 0.0;
    double duration = 0.0;
    double offset = 0.0;
    double rate = 1.0;
    Map* properties = nullptr;
    bool camera = false;
    bool isEffect = false;
    bool isGroup = false;
    bool isAudio = false;
    // A musical clip under a tempo map follows the beats exactly.
    std::optional<ClipTiming::BeatWarp> warp;
    // The Scope's picture (Project only), in project time like a camera.
    bool beam = false;
    // On a locked track: shown, but not edited.
    bool locked = false;

    double end() const { return start + duration; }
    ClipTiming clock() const {
        ClipTiming timing(start, start + duration, offset, rate);
        timing.warp = warp;
        return timing;
    }
    double localTime(double projectTime) const { return clock().localTime(projectTime); }
    // Scope time of a content-local time (the inverse of localTime).
    double projectTime(double local) const { return clock().projectTime(local); }
    auto curve(std::string_view property) const -> std::conditional_t<std::is_const_v<Map>, const Curve*, Curve*> {
        if (properties == nullptr) {
            return nullptr;
        }
        const auto found = properties->find(property);
        return found == properties->end() ? nullptr : &found->second;
    }
};

using PropertyTarget = BasicPropertyTarget<const PropertyMap>;
using MutablePropertyTarget = BasicPropertyTarget<PropertyMap>;

// Project-shaped input keeps this timing/lookup helper independent of JUCE and
// document ownership. Both const and mutable views preserve caller constness.
template <typename ProjectType>
auto findPropertyTarget(ProjectType& project, Id id) -> std::optional<BasicPropertyTarget<std::conditional_t<std::is_const_v<ProjectType>, const PropertyMap, PropertyMap>>> {
    using Target = BasicPropertyTarget<std::conditional_t<std::is_const_v<ProjectType>, const PropertyMap, PropertyMap>>;
    if (id == 0) {
        return std::nullopt;
    }
    for (auto& effect : project.effects) {
        if (effect.id == id) {
            return Target { .id = effect.id, .name = effect.name, .duration = project.duration, .properties = &effect.properties, .isEffect = true };
        }
    }
    for (auto& group : project.groups) {
        if (group.id == id) {
            return Target { .id = group.id, .name = group.name, .duration = project.duration, .properties = &group.properties, .isGroup = true };
        }
        for (auto& effect : group.effects) {
            if (effect.id == id) {
                return Target { .id = effect.id, .name = effect.name, .duration = project.duration, .properties = &effect.properties, .isEffect = true };
            }
        }
    }
    for (std::size_t index = 0; index < project.tracks.size(); ++index) {
        const auto& candidate = project.tracks[index];
        const auto ownsId = [id](const auto& owner) { return owner.id == id || std::any_of(owner.effects.begin(), owner.effects.end(), [id](const auto& effect) { return effect.id == id; }); };
        if (std::none_of(candidate.effects.begin(), candidate.effects.end(), [id](const auto& effect) { return effect.id == id; })
            && std::none_of(candidate.clips.begin(), candidate.clips.end(), ownsId)) {
            continue;
        }
        auto& track = itemAt(project.tracks, index);
        for (auto& effect : track.effects) {
            if (effect.id == id) {
                return Target { .id = effect.id, .name = effect.name, .duration = project.duration, .properties = &effect.properties, .isEffect = true, .locked = track.locked };
            }
        }
        for (auto& clip : track.clips) {
            const auto effect = std::find_if(clip.effects.begin(), clip.effects.end(), [id](const auto& item) { return item.id == id; });
            if (clip.id != id && effect == clip.effects.end()) { continue; }
            // Only the owner's timing: under a tempo map it is not free.
            const auto timing = clip.timing(project.tempo());
            const auto owner = effect != clip.effects.end();
            return Target { .id = owner ? effect->id : clip.id, .name = owner ? std::string_view(effect->name) : std::string_view(clip.name), .start = timing.start, .duration = timing.duration(),
                            .offset = timing.offset, .rate = timing.rate, .properties = owner ? &effect->properties : &clip.properties, .isEffect = owner, .isAudio = !owner && track.kind == TrackKind::audio,
                            .warp = timing.warp, .locked = track.locked };
        }
    }
    for (auto& camera : project.cameras) {
        if (camera.id == id) {
            return Target { .id = camera.id, .name = camera.name, .duration = project.duration, .properties = &camera.properties, .camera = true };
        }
    }
    if constexpr (requires { project.beam; }) {
        if (project.beam.id == id) {
            return Target { .id = project.beam.id, .name = "Scope", .duration = project.duration, .properties = &project.beam.properties, .beam = true };
        }
    }
    return std::nullopt;
}

template <typename ProjectType>
auto findPropertyCurve(ProjectType& project, Id id, std::string_view property) -> std::conditional_t<std::is_const_v<ProjectType>, const Curve*, Curve*> {
    const auto target = findPropertyTarget(project, id);
    return target.has_value() ? target->curve(property) : nullptr;
}
}
