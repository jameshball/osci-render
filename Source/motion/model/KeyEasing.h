#pragma once

#include "KeyEdit.h"

namespace motion {
// After Effects' Easy Ease on one key: its speed falls to zero with a third of
// each eased segment as influence. `in` eases the segment arriving at the key,
// `out` the one leaving it. The far end of each segment keeps its shape: a
// linear neighbour still leaves in a straight line, and a hold into the key
// still holds. Easing out of a hold key makes it a Bezier, as in After Effects.
// Returns false when the key is missing or already eased that way.
inline bool easeKey(Curve& curve, double time, bool in, bool out) {
    const auto keys = curve.keyframes();
    const auto found = std::find_if(keys.begin(), keys.end(), [time](const Keyframe& key) { return keyedit::sameTime(key.time, time); });
    if (found == keys.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - keys.begin());
    // The slope a segment had at one end before it became a Bezier.
    const auto endSlope = [&](std::size_t from, std::size_t at) {
        const auto& start = keys[from];
        const auto& end = keys[from + 1];
        if (start.interpolation == Interpolation::linear) {
            return (end.value - start.value) / (end.time - start.time);
        }
        return start.interpolation == Interpolation::cubic ? (at == from ? start.outgoingSlope : end.incomingSlope) : curve.automaticSlope(at);
    };
    auto key = *found;
    std::vector<Keyframe> changed;
    if (in && index > 0 && keys[index - 1].interpolation != Interpolation::hold) {
        auto previous = keys[index - 1];
        if (previous.interpolation != Interpolation::cubic) {
            previous.outgoingSlope = endSlope(index - 1, index - 1);
            previous.outgoingInfluence = Keyframe::defaultInfluence;
            previous.interpolation = Interpolation::cubic;
            changed.push_back(previous);
        }
        key.incomingSlope = 0;
        key.incomingInfluence = Keyframe::defaultInfluence;
    }
    if (out && index + 1 < keys.size()) {
        if (key.interpolation != Interpolation::cubic) {
            auto next = keys[index + 1];
            next.incomingSlope = endSlope(index, index + 1);
            next.incomingInfluence = Keyframe::defaultInfluence;
            changed.push_back(next);
        }
        key.interpolation = Interpolation::cubic;
        key.outgoingSlope = 0;
        key.outgoingInfluence = Keyframe::defaultInfluence;
    }
    const auto& original = *found;
    if (changed.empty() && key.interpolation == original.interpolation && key.incomingSlope == original.incomingSlope && key.outgoingSlope == original.outgoingSlope
        && key.incomingInfluence == original.incomingInfluence && key.outgoingInfluence == original.outgoingInfluence) {
        return false;
    }
    for (const auto& neighbour : changed) {
        curve.setKey(neighbour);
    }
    curve.setKey(key);
    return true;
}
}
