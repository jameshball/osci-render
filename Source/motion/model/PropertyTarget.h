#pragma once

#include "Camera.h"
#include "Group.h"
#include <string_view>
#include <type_traits>

namespace motion {
using PropertyMap = std::map<std::string, Curve>;

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
    double contentBpm = 0;
    // A musical clip under a tempo map follows the beats exactly.
    std::optional<ClipTiming::BeatWarp> warp;
    double curveBpm(double projectBpm) const { return contentBpm > 0 ? contentBpm : projectBpm; }

    double end() const { return start + duration; }
    ClipTiming clock() const {
        ClipTiming timing(start, start + duration, offset, rate);
        timing.warp = warp;
        return timing;
    }
    double localTime(double projectTime) const { return clock().localTime(projectTime); }
    // Scope time of a content-local time (the inverse of localTime).
    double projectTime(double local) const { return clock().projectTime(local); }
    auto curve(const std::string& property) const -> std::conditional_t<std::is_const_v<Map>, const Curve*, Curve*> {
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
            return Target { effect.id, effect.name, 0.0, project.duration, 0.0, 1.0, &effect.properties, false, true };
        }
    }
    for (auto& group : project.groups) {
        if (group.id == id) {
            return Target { group.id, group.name, 0.0, project.duration, 0.0, 1.0, &group.properties, false, false, true };
        }
        for (auto& effect : group.effects) {
            if (effect.id == id) {
                return Target { effect.id, effect.name, 0.0, project.duration, 0.0, 1.0, &effect.properties, false, true };
            }
        }
    }
    for (auto& track : project.tracks) {
        for (auto& effect : track.effects) {
            if (effect.id == id) {
                return Target { effect.id, effect.name, 0.0, project.duration, 0.0, 1.0, &effect.properties, false, true };
            }
        }
        for (auto& clip : track.clips) {
            const auto timing = clip.timing(project.tempo());
            for (auto& effect : clip.effects) {
                if (effect.id == id) {
                    return Target { effect.id, effect.name, timing.start, timing.duration(), timing.offset, timing.rate, &effect.properties, false, true, false, false, clip.curveBpm(project.tempo()), timing.warp };
                }
            }
            if (clip.id == id) {
                return Target { clip.id, clip.name, timing.start, timing.duration(), timing.offset, timing.rate, &clip.properties, false, false, false, track.kind == TrackKind::audio, clip.curveBpm(project.tempo()), timing.warp };
            }
        }
    }
    for (auto& camera : project.cameras) {
        if (camera.id == id) {
            return Target { camera.id, camera.name, 0.0, project.duration, 0.0, 1.0, &camera.properties, true };
        }
    }
    return std::nullopt;
}

template <typename ProjectType>
auto findPropertyCurve(ProjectType& project, Id id, const std::string& property) -> std::conditional_t<std::is_const_v<ProjectType>, const Curve*, Curve*> {
    const auto target = findPropertyTarget(project, id);
    return target.has_value() ? target->curve(property) : nullptr;
}
}
