#pragma once

#include "../model/Animation.h"

namespace motion {

// Structural constancy over a closed content-time interval. This does not
// infer constancy from sampled values, clamps, cancellation between curves or
// repeating modulation phases. Intended for off-thread allocation preparation;
// no allocation occurs, and only intersecting segments are walked after the
// curve's validity check and a binary search. Caller owns clip/group time maps.
inline bool curveConstantOnInterval(const Curve& curve, double start, double end) {
    if (!std::isfinite(start) || !std::isfinite(end) || start > end || !curve.valid()) { return false; }
    if (curve.modulation.enabled && curve.modulation.amount != 0) { return false; }
    const auto& keys = curve.keyframes();
    if (keys.empty()) { return true; }
    const auto constantSegment = [](const Keyframe& left, const Keyframe& right) {
        if (left.interpolation == Interpolation::hold) { return true; }
        const auto duration = right.time - left.time;
        if (!std::isfinite(duration) || duration <= 0 || left.value != right.value) { return false; }
        if (left.interpolation == Interpolation::cubic) {
            return left.outgoingSlope == 0 && right.incomingSlope == 0;
        }
        return left.interpolation == Interpolation::linear || left.interpolation == Interpolation::smooth;
    };
    auto right = std::upper_bound(keys.begin(), keys.end(), start,
        [](double time, const Keyframe& key) { return time < key.time; });
    const auto value = right == keys.begin() ? keys.front().value : (right - 1)->value;
    auto cursor = start;
    while (right != keys.end() && right->time <= end) {
        if (cursor < right->time && right != keys.begin() && !constantSegment(*(right - 1), *right)) { return false; }
        // At a key time evaluateBase selects that key, including hold jumps.
        if (right->value != value) { return false; }
        cursor = right->time;
        ++right;
    }
    if (cursor < end && right != keys.begin() && right != keys.end() && !constantSegment(*(right - 1), *right)) { return false; }
    return true;
}

}
