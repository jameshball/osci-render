#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
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

    double evaluate(double time) const {
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
        const auto position = std::lower_bound(keys.begin(), keys.end(), key.time,
            [](const Keyframe& existing, double time) { return existing.time < time; });
        if (position != keys.end() && position->time == key.time) {
            *position = key;
        } else {
            keys.insert(position, key);
        }
    }

    bool removeKey(double time) {
        const auto position = std::lower_bound(keys.begin(), keys.end(), time,
            [](const Keyframe& key, double value) { return key.time < value; });
        if (position == keys.end() || position->time != time) {
            return false;
        }
        keys.erase(position);
        return true;
    }

    // Editing a keyed value must not erase its interpolation or tangents.
    void setKeyValue(double time, double value) {
        const auto found = std::lower_bound(keys.begin(), keys.end(), time,
            [](const Keyframe& key, double at) { return key.time < at; });
        auto key = found != keys.end() && found->time == time ? *found : Keyframe { time, value };
        key.value = value;
        setKey(key);
    }

    bool animated() const { return !keys.empty(); }
    const std::vector<Keyframe>& keyframes() const { return keys; }
    double base = 0.0;

private:
    std::vector<Keyframe> keys;
};

}
