#pragma once

#include "PropertyTarget.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace motion {

// A key on one of several curves edited together, at its content-local time.
struct CurveKey {
    std::string property;
    double time = 0.0;
    bool operator==(const CurveKey&) const = default;
};

struct KeyEditResult {
    PropertyMap curves;
    std::vector<CurveKey> selection; // The input selection's keys at their new times, in input order.
};

namespace keyedit {

// Matches Curve's own tolerance, so a moved key never silently merges with another.
inline bool sameTime(double a, double b) {
    return std::abs(a - b) <= 32 * std::numeric_limits<double>::epsilon() * std::max({ 1.0, std::abs(a), std::abs(b) });
}

inline const Keyframe* findKey(const Curve& curve, double time) {
    const auto& keys = curve.keyframes();
    const auto found = std::find_if(keys.begin(), keys.end(), [time](const Keyframe& key) { return sameTime(key.time, time); });
    return found != keys.end() ? &*found : nullptr;
}

// Retimes the selected keys of every curve with mapTime and offsets their values
// by valueDelta (then constrain(property, value) when given). The selection moves
// as a unit, so key counts never change: the edit is rejected when a new time is
// not finite, leaves [minimum, maximum], or lands on an unselected key or on
// another moved key of the same curve.
inline std::optional<KeyEditResult> transformKeys(const PropertyMap& curves, const std::vector<CurveKey>& selection, const std::function<double(double)>& mapTime, double valueDelta, double minimum, double maximum, const std::function<double(const std::string&, double)>& constrain = {}) {
    KeyEditResult result { curves, {} };
    const auto tolerance = 1.0e-9 * std::max({ 1.0, std::abs(minimum), std::abs(maximum) });
    std::vector<std::pair<std::string, Keyframe>> moved;
    for (const auto& ref : selection) {
        const auto found = curves.find(ref.property);
        const auto* original = found != curves.end() ? findKey(found->second, ref.time) : nullptr;
        if (original == nullptr) {
            return std::nullopt;
        }
        auto key = *original;
        key.time = mapTime(original->time);
        key.value = original->value + valueDelta;
        if (constrain) {
            key.value = constrain(ref.property, key.value);
        }
        if (!std::isfinite(key.time) || !std::isfinite(key.value) || key.time < minimum - tolerance || key.time > maximum + tolerance) {
            return std::nullopt;
        }
        result.curves[ref.property].removeKey(original->time);
        moved.emplace_back(ref.property, key);
        result.selection.push_back({ ref.property, key.time });
    }
    for (const auto& [property, key] : moved) {
        auto& curve = result.curves[property];
        if (findKey(curve, key.time) != nullptr) {
            return std::nullopt;
        }
        curve.setKey(key);
    }
    return result;
}

inline std::optional<KeyEditResult> moveKeys(const PropertyMap& curves, const std::vector<CurveKey>& selection, double timeDelta, double valueDelta, double minimum, double maximum, const std::function<double(const std::string&, double)>& constrain = {}) {
    return transformKeys(curves, selection, [timeDelta](double time) { return time + timeDelta; }, valueDelta, minimum, maximum, constrain);
}

// Scales key times about pivot so keys at `from` land exactly on `to`. The pivot
// edge stays put and order is kept; `to` crossing or reaching the pivot is rejected.
inline std::optional<KeyEditResult> scaleKeyTimes(const PropertyMap& curves, const std::vector<CurveKey>& selection, double pivot, double from, double to, double minimum, double maximum) {
    const auto factor = (to - pivot) / (from - pivot);
    if (!std::isfinite(factor) || factor <= 0.0) {
        return std::nullopt;
    }
    return transformKeys(curves, selection, [pivot, from, to, factor](double time) {
        if (sameTime(time, from)) {
            return to;
        }
        return sameTime(time, pivot) ? time : pivot + (time - pivot) * factor;
    }, 0.0, minimum, maximum);
}

} // namespace keyedit
} // namespace motion
