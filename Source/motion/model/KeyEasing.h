#pragma once

#include "KeyEdit.h"

namespace motion {
// Eased (Easy Ease or similar): a Bezier key that stops on its outgoing side.
inline bool isEased(const Keyframe& key) {
    return key.interpolation == Interpolation::cubic && key.outgoingSlope == 0;
}

// After Effects' Easy Ease on one key: its speed falls to zero with a third of
// each eased segment as influence. `in` eases the segment arriving at the key,
// `out` the one leaving it. The far end of each segment keeps its shape: a
// linear neighbour still leaves in a straight line, and a hold into the key
// still holds. Easing out of a hold key makes it a Bezier, as in After Effects.
// Returns false when the key is missing or already eased that way.
inline bool easeKey(Curve& curve, double time, bool in, bool out) {
    const auto* found = curve.findKey(time);
    if (found == nullptr) {
        return false;
    }
    const auto keys = curve.keyframes();
    const auto index = static_cast<std::size_t>(found - curve.keyframes().data());
    auto key = keys[index];
    std::vector<Keyframe> changed;
    if (in && index > 0 && keys[index - 1].interpolation != Interpolation::hold) {
        auto previous = keys[index - 1];
        if (previous.interpolation != Interpolation::cubic) {
            previous.outgoingSlope = keyedit::segmentSlope(curve, index - 1, index - 1);
            previous.outgoingInfluence = Keyframe::defaultInfluence;
            previous.interpolation = Interpolation::cubic;
            changed.push_back(previous);
        }
        key.incomingSlope = 0;
        key.incomingInfluence = Keyframe::defaultInfluence;
    }
    // A last key has no segment to ease but is marked eased, as in After Effects.
    if (out) {
        if (index + 1 < keys.size() && key.interpolation != Interpolation::cubic) {
            auto next = keys[index + 1];
            next.incomingSlope = keyedit::segmentSlope(curve, index, index + 1);
            next.incomingInfluence = Keyframe::defaultInfluence;
            changed.push_back(next);
        }
        key.interpolation = Interpolation::cubic;
        key.outgoingSlope = 0;
        key.outgoingInfluence = Keyframe::defaultInfluence;
    }
    const auto& original = keys[index];
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
