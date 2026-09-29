#pragma once

#include "Timeline.h"
#include <array>
#include <set>

namespace motion {
inline constexpr std::array<const char*, 13> propertyNames {
    "position.x", "position.y", "position.z", "rotation.x", "rotation.y", "rotation.z",
    "scale.x", "scale.y", "scale.z", "red", "green", "blue", "weight"
};
inline constexpr std::size_t maximumGroupDepth = 32;

struct Group {
    Group() {
        for (std::size_t index = 0; index < propertyNames.size(); ++index) {
            properties.emplace(propertyNames[index], Curve(index >= 6 ? 1 : 0));
        }
    }
    Id id = 0;
    std::string name = "Group";
    Id parent = 0;
    std::map<std::string, Curve> properties;
    std::vector<EffectInstance> effects;
    bool muted = false, solo = false;
    bool spatialPath = false, quaternionRotation = false;

    bool valid() const {
        if (id == 0 || parent == id || properties.size() != propertyNames.size()) {
            return false;
        }
        for (std::size_t index = 0; index < propertyNames.size(); ++index) {
            const auto found = properties.find(propertyNames[index]);
            if (found == properties.end() || !found->second.valid()) {
                return false;
            }
            const auto validValue = [index](double value) {
                return std::isfinite(value) && (index < 9 || (value >= 0 && value <= (index == 12 ? 1000000 : 1)));
            };
            if (!validValue(found->second.base)) {
                return false;
            }
            for (const auto& key : found->second.keyframes()) {
                if (!key.valid() || !validValue(key.value)) {
                    return false;
                }
            }
        }
        return true;
    }
};

template <typename ProjectType>
auto findGroup(ProjectType& project, Id id) -> std::conditional_t<std::is_const_v<ProjectType>, const Group*, Group*> {
    for (auto& group : project.groups) {
        if (group.id == id && id != 0) {
            return &group;
        }
    }
    return nullptr;
}

template <typename ProjectType>
bool validGroupHierarchy(const ProjectType& project) {
    std::set<Id> identities;
    for (const auto& group : project.groups) {
        if (!group.valid() || !identities.insert(group.id).second) {
            return false;
        }
        Id current = group.id;
        std::size_t depth = 0;
        while (current != 0) {
            const auto* ancestor = findGroup(project, current);
            if (ancestor == nullptr || ++depth > maximumGroupDepth) {
                return false;
            }
            current = ancestor->parent;
        }
    }
    for (const auto& track : project.tracks) {
        if (track.group != 0 && findGroup(project, track.group) == nullptr) {
            return false;
        }
    }
    return true;
}

template <typename ProjectType>
bool trackIsAudible(const ProjectType& project, const Track& track) {
    if (track.muted) {
        return false;
    }
    const bool anySolo = std::any_of(project.tracks.begin(), project.tracks.end(), [](const auto& value) { return value.solo; })
        || std::any_of(project.groups.begin(), project.groups.end(), [](const auto& value) { return value.solo; });
    bool inSolo = track.solo;
    auto current = track.group;
    std::size_t depth = 0;
    while (current != 0) {
        const auto* group = findGroup(project, current);
        if (group == nullptr || group->muted || ++depth > maximumGroupDepth) {
            return false;
        }
        inSolo = inSolo || group->solo;
        current = group->parent;
    }
    return !anySolo || inSolo;
}
}
