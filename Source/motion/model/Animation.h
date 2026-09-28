#pragma once

#include "Modulation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <vector>

namespace motion {

// The left key's interpolation shapes each segment. smooth: automatic clamped
// tangents at both ends (continuous velocity, never overshooting an extreme).
// cubic: the left key's outgoing and right key's incoming authored Bezier
// handles, each a slope plus a time influence (fraction of the segment); an
// influence of 1/3 on both is a plain Hermite segment.
enum class Interpolation { hold, linear, smooth, cubic };

struct Keyframe {
    static constexpr double defaultInfluence = 1.0 / 3.0;
    double time = 0.0;
    double value = 0.0;
    Interpolation interpolation = Interpolation::smooth;
    double incomingSlope = 0.0;
    double outgoingSlope = 0.0;
    double incomingInfluence = defaultInfluence;
    double outgoingInfluence = defaultInfluence;

    static bool validInfluence(double value) { return std::isfinite(value) && value > 0.0 && value <= 1.0; }
    bool valid() const {
        return std::isfinite(time) && std::isfinite(value) && std::isfinite(incomingSlope) && std::isfinite(outgoingSlope)
            && validInfluence(incomingInfluence) && validInfluence(outgoingInfluence)
            && static_cast<int>(interpolation) >= 0 && static_cast<int>(interpolation) <= 3;
    }
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
        const auto index = static_cast<std::size_t>(right - keys.begin()) - 1;
        const auto& left = keys[index];
        const auto span = right->time - left.time;
        const auto t = (time - left.time) / span;
        switch (left.interpolation) {
            case Interpolation::hold: return left.value;
            case Interpolation::linear: return left.value + (right->value - left.value) * t;
            case Interpolation::smooth:
                return hermite(left.value, right->value, span, t, autoSlopes[index], autoSlopes[index + 1]);
            case Interpolation::cubic: {
                // A flat segment is exactly constant, free of rounding wobble.
                if (left.value == right->value && left.outgoingSlope == 0 && right->incomingSlope == 0) { return left.value; }
                if (left.outgoingInfluence == Keyframe::defaultInfluence && right->incomingInfluence == Keyframe::defaultInfluence) {
                    return hermite(left.value, right->value, span, t, left.outgoingSlope, right->incomingSlope);
                }
                return bezier(left.value, right->value, span, t, left.outgoingSlope, right->incomingSlope, left.outgoingInfluence, right->incomingInfluence);
            }
        }
        return left.value;
    }

    // The automatic tangent at a key, used when converting smooth to authored.
    double automaticSlope(std::size_t index) const { return index < autoSlopes.size() ? autoSlopes[index] : 0.0; }

    void setKey(Keyframe key) {
        if (!key.valid()) {
            throw std::invalid_argument("Keyframe values must be finite with influences in (0, 1]");
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
        refreshTangents();
    }

    bool removeKey(double time) {
        const auto position = matchingKey(time);
        if (position == keys.end()) {
            return false;
        }
        keys.erase(position);
        refreshTangents();
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
        return std::all_of(keys.begin(), keys.end(), [](const Keyframe& key) { return key.valid(); });
    }
    bool animated() const { return !keys.empty(); }
    const std::vector<Keyframe>& keyframes() const { return keys; }
    double base = 0.0;
    Modulation modulation;

private:
    static double hermite(double a, double b, double span, double t, double outSlope, double inSlope) {
        const auto t2 = t * t, t3 = t2 * t;
        return (2.0 * t3 - 3.0 * t2 + 1.0) * a + (t3 - 2.0 * t2 + t) * span * outSlope
            + (-2.0 * t3 + 3.0 * t2) * b + (t3 - t2) * span * inSlope;
    }
    // Time-parametric Bezier: solve x(u) = t on the normalized segment, where
    // handle influences keep x monotone, then evaluate the value polynomial.
    static double bezier(double a, double b, double span, double t, double outSlope, double inSlope, double outInfluence, double inInfluence) {
        const auto x1 = outInfluence, x2 = 1.0 - inInfluence;
        const auto y1 = a + outSlope * outInfluence * span, y2 = b - inSlope * inInfluence * span;
        const auto curve = [](double p0, double p1, double p2, double p3, double u) {
            const auto v = 1.0 - u;
            return v * v * v * p0 + 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u * p3;
        };
        double low = 0, high = 1, u = t;
        for (int iteration = 0; iteration < 24; ++iteration) {
            const auto x = curve(0, x1, x2, 1, u);
            if (std::abs(x - t) < 1.0e-12) { break; }
            if (x < t) { low = u; } else { high = u; }
            const auto v = 1.0 - u;
            const auto derivative = 3.0 * v * v * x1 + 6.0 * v * u * (x2 - x1) + 3.0 * u * u * (1.0 - x2);
            const auto next = derivative > 1.0e-9 ? u - (x - t) / derivative : 0.5 * (low + high);
            u = next > low && next < high ? next : 0.5 * (low + high);
        }
        return curve(a, y1, y2, b, u);
    }
    // Auto-clamped tangents: Catmull-Rom limited per Fritsch-Carlson so each
    // smooth segment is monotone between monotone keys; extremes stay flat.
    void refreshTangents() {
        autoSlopes.assign(keys.size(), 0.0);
        for (std::size_t k = 1; k + 1 < keys.size(); ++k) {
            const auto& previous = keys[k - 1];
            const auto& current = keys[k];
            const auto& next = keys[k + 1];
            const auto before = (current.value - previous.value) / (current.time - previous.time);
            const auto after = (next.value - current.value) / (next.time - current.time);
            if (!std::isfinite(before) || !std::isfinite(after) || before * after <= 0) { continue; }
            const auto central = (next.value - previous.value) / (next.time - previous.time);
            const auto limit = 3.0 * std::min(std::abs(before), std::abs(after));
            autoSlopes[k] = std::copysign(std::min(std::abs(central), limit), central);
        }
    }
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
    std::vector<double> autoSlopes;
};

}
