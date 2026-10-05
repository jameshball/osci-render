#pragma once

#include "EditorTransformFrame.h"
#include <map>

namespace motion::editor {
struct MotionPathPoint {
    double time = 0;
    Vec3 position;
    bool breakBefore = false, key = false, dot = false;
    double contentTime = 0;
};
struct MotionPath {
    std::vector<MotionPathPoint> points;
    bool tooComplex = false;
};

// An authoring guide for the transformed origin, before effect processing or
// camera projection. This is evaluated only when the document/selection changes.
template <typename ProjectType>
MotionPath buildMotionPath(const ProjectType& project, Id selection) {
    const Clip* selected = nullptr;
    Id parentId = 0;
    for (const auto& track : project.tracks) {
        if (track.kind != TrackKind::visual) { continue; }
        for (const auto& clip : track.clips) {
            if (clip.id == selection) { selected = &clip; parentId = track.group; break; }
        }
        if (selected != nullptr) { break; }
    }
    if (selected == nullptr) { return {}; }
    const auto timing = selected->timing(project.tempo());
    const auto start = std::max(0.0, timing.start), end = std::min(project.duration, timing.end());
    if (!timing.valid() || !(end > start)) { return {}; }
    std::vector<const Group*> parents;
    while (parentId != 0) {
        const auto* group = findGroup(project, parentId);
        if (group == nullptr || parents.size() == maximumGroupDepth
            || std::find(parents.begin(), parents.end(), group) != parents.end()) { return {}; }
        parents.push_back(group);
        parentId = group->parent;
    }
    struct Flags { bool key = false, jump = false, dot = false; double contentTime = 0; };
    std::map<double, Flags> times;
    constexpr int steps = 256;
    for (int index = 0; index <= steps; ++index) {
        times[start + (end - start) * index / steps].dot = index % 4 == 0;
    }
    std::size_t keyCount = 0;
    const auto collect = [&](const auto& properties, int axes, bool own) {
        for (int axis = 0; axis < axes; ++axis) {
            const auto found = properties.find(objectPropertySpecs[static_cast<std::size_t>(axis)].id);
            if (found == properties.end()) { continue; }
            const Keyframe* previous = nullptr;
            for (const auto& key : found->second.keyframes()) {
                if (++keyCount > 2048) { return false; }
                const auto time = own ? timing.projectTime(key.time) : key.time;
                if (time >= start && time <= end) {
                    auto& flags = times[time];
                    flags.key = flags.key || own;
                    if (own) { flags.contentTime = key.time; }
                    if (previous != nullptr && previous->interpolation == Interpolation::hold && previous->value != key.value) {
                        flags.jump = true;
                        if (time > start) { times[std::nextafter(time, start)]; }
                    }
                }
                previous = &key;
            }
        }
        return true;
    };
    if (!collect(selected->properties, 3, true)) { return {{}, true}; }
    for (const auto* group : parents) {
        if (!collect(group->properties, 9, false)) { return {{}, true}; }
    }
    const auto value = [](const auto& properties, int index, double time, double bpm) {
        const auto found = properties.find(objectPropertySpecs[static_cast<std::size_t>(index)].id);
        return found == properties.end() ? objectPropertySpecs[static_cast<std::size_t>(index)].defaultValue : found->second.evaluate(time, bpm);
    };
    MotionPath path;
    path.points.reserve(times.size());
    bool broken = true;
    for (const auto& [time, flags] : times) {
        const auto local = selected->localTime(time, project.tempo());
        const auto bpm = selected->curveBpm(project.tempo());
        Vec3 position {value(selected->properties, 0, local, bpm), value(selected->properties, 1, local, bpm), value(selected->properties, 2, local, bpm)};
        for (const auto* group : parents) {
            std::array<double, 9> values;
            for (int axis = 0; axis < 9; ++axis) { values[static_cast<std::size_t>(axis)] = value(group->properties, axis, time, project.bpm); }
            constexpr auto radians = std::numbers::pi / 180.0;
            const transform_detail::Affine transform {{values[0], values[1], values[2]}, {values[3] * radians, values[4] * radians, values[5] * radians}, {values[6], values[7], values[8]}};
            position = transform.direction(position) + transform.position;
        }
        if (!position.finite()) { broken = true; continue; }
        path.points.push_back({time, position, broken || flags.jump, flags.key, flags.dot, flags.contentTime});
        broken = false;
    }
    return path;
}
}
