#pragma once

#include "Modulation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <vector>

namespace motion {

enum class Interpolation { hold, linear, smooth, cubic };

struct Keyframe {
    double time = 0.0;
    double value = 0.0;
    Interpolation interpolation = Interpolation::smooth;
    double incomingSlope = 0.0;
    double outgoingSlope = 0.0;
};

// Curve time is content-local. Clip placement never rewrites its keys.
class Curve {
public:
    Curve() = default;
    explicit Curve(double initialValue) : base(initialValue) {}

    double evaluate(double time, double bpm = 120) const {
        const auto authored = evaluateBase(time);
        const auto baseValue = std::isfinite(authored) ? authored : (std::isfinite(base) ? base : 0.0);
        if (!modulation.enabled || !modulation.valid()) {
            return baseValue;
        }
        const auto movement = modulation.amount * modulation.value(time, bpm);
        const auto value = modulation.mode == ModulationMode::add ? baseValue + movement : baseValue * (1 + movement);
        return std::isfinite(value) ? value : baseValue;
    }

    double evaluateBase(double time) const {
        if (!std::isfinite(time)) {
            return base;
        }
        if (keys.empty()) {
            return base;
        }
        const auto right = std::upper_bound(keys.begin(), keys.end(), time,
            [](double value, const Keyframe& key) { return value < key.time; });
        if (right == keys.begin()) {
            return right->value;
        }
        if (right == keys.end()) {
            return keys.back().value;
        }
        const auto& left = *(right - 1);
        const auto span = right->time - left.time;
        const auto t = (time - left.time) / span;
        switch (left.interpolation) {
            case Interpolation::hold: return left.value;
            case Interpolation::linear: return left.value + (right->value - left.value) * t;
            case Interpolation::smooth: return left.value + (right->value - left.value) * t * t * (3.0 - 2.0 * t);
            case Interpolation::cubic: {
                // A flat Hermite segment is exactly constant. Avoid tiny
                // cancellation differences that can move integer beam slots.
                if (left.value == right->value && left.outgoingSlope == 0 && right->incomingSlope == 0) { return left.value; }
                const auto t2 = t * t;
                const auto t3 = t2 * t;
                return (2.0 * t3 - 3.0 * t2 + 1.0) * left.value
                    + (t3 - 2.0 * t2 + t) * span * left.outgoingSlope
                    + (-2.0 * t3 + 3.0 * t2) * right->value
                    + (t3 - t2) * span * right->incomingSlope;
            }
        }
        return left.value;
    }

    void setKey(Keyframe key) {
        if (!std::isfinite(key.time) || !std::isfinite(key.value)
            || !std::isfinite(key.incomingSlope) || !std::isfinite(key.outgoingSlope)) {
            throw std::invalid_argument("Keyframe values must be finite");
        }
        const auto existing = matchingKey(key.time);
        if (existing != keys.end()) {
            key.time = existing->time;
            *existing = key;
        } else {
            const auto position = std::lower_bound(keys.begin(), keys.end(), key.time,
                [](const Keyframe& value, double time) { return value.time < time; });
            keys.insert(position, key);
        }
    }

    bool removeKey(double time) {
        const auto position = matchingKey(time);
        if (position == keys.end()) {
            return false;
        }
        keys.erase(position);
        return true;
    }

    // Editing a keyed value must not erase its interpolation or tangents.
    void setKeyValue(double time, double value) {
        const auto found = matchingKey(time);
        auto key = found != keys.end() ? *found : Keyframe { time, value };
        key.value = value;
        setKey(key);
    }

    bool valid() const {
        if (!std::isfinite(base) || !modulation.valid()) {
            return false;
        }
        for (const auto& key : keys) {
            if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.incomingSlope)
                || !std::isfinite(key.outgoingSlope) || static_cast<int>(key.interpolation) < 0 || static_cast<int>(key.interpolation) > 3) {
                return false;
            }
        }
        return true;
    }
    bool animated() const { return !keys.empty(); }
    const std::vector<Keyframe>& keyframes() const { return keys; }
    double base = 0.0;
    Modulation modulation;

private:
    std::vector<Keyframe>::iterator matchingKey(double time) {
        if (!std::isfinite(time)) { return keys.end(); }
        const auto sameTime = [time](const Keyframe& key) {
            // Decimal persistence and project-to-source subtraction can differ
            // by a few ULPs. Preserve the authored time, not a frame-sized snap.
            const auto tolerance = 32 * std::numeric_limits<double>::epsilon()
                * std::max({1.0, std::abs(time), std::abs(key.time)});
            return std::abs(key.time - time) <= tolerance;
        };
        const auto next = std::lower_bound(keys.begin(), keys.end(), time,
            [](const Keyframe& key, double value) { return key.time < value; });
        if (next != keys.end() && sameTime(*next)) { return next; }
        if (next != keys.begin() && sameTime(*(next - 1))) { return next - 1; }
        return keys.end();
    }
    std::vector<Keyframe> keys;
};

}
