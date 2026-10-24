#pragma once

#include "../model/Effects.h"
#include "TimeCache.h"
#include "../../../modules/osci_render_core/effects/osci_PointEffectKernels.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace motion {
namespace copies {
// Moves `input` up to a plane through the origin with outward `normal`.
inline osci::Point clipToPlane(osci::Point input, osci::Point normal) {
    const auto distance = input.innerProduct(normal);
    return input + (std::max<double>(0.0, distance) - distance) * normal;
}

// Segment `copy` of `segments` (osci-render's Kaleidoscope, one segment a
// copy rather than one a pass): the shape stood on the +X radius, spread out
// along it, every other one mirrored, kept within its wedge, then turned to
// its place (and all turned by `turn` turns).
inline osci::Point kaleidoscope(osci::Point input, int copy, double segments, double mirror, double spread, double clip, double turn) {
    const auto twoPi = 2 * std::numbers::pi;
    auto output = (1 - spread) * osci::Point(input.y, -input.x, input.z);
    output.x += spread;
    if (copy % 2 == 1) { output.y *= 1 - 2 * mirror; }
    const auto size = twoPi / segments;
    const osci::Point upper(std::sin(0.5 * size), -std::cos(0.5 * size), 0);
    const osci::Point lower(upper.x, -upper.y, 0);
    osci::Point clipped;
    // What falls outside the wedge on its far side is folded to the middle,
    // where every segment meets: drawn dark there, or the beam would rest
    // on one bright spot.
    auto light = 1.0;
    if (size < std::numbers::pi) {
        clipped = clipToPlane(clipToPlane(output, upper), lower);
        if (clipped.x < 0) {
            clipped.x = 0;
            clipped.y = 0;
            light = 1 - clip;
        }
    } else {
        clipped = clipToPlane(output, output.y > 0 ? upper : lower);
    }
    output = (1 - clip) * output + clip * clipped;
    output.rotate(0, 0, (copy / segments + turn) * twoPi);
    return output.withColour(static_cast<float>(input.r * light), static_cast<float>(input.g * light), static_cast<float>(input.b * light));
}

// Copy `copy` of `count` (osci-render's Duplicator): smaller by `spread`
// and pushed out that far towards its place on a circle.
inline osci::Point duplicate(osci::Point input, int copy, double count, double spread, double turn) {
    const auto theta = (copy / count + turn) * 2 * std::numbers::pi;
    return ((1 - spread) * input + spread * osci::Point(std::cos(theta), std::sin(theta), 0.0)).withColour(input.r, input.g, input.b);
}
}

struct PreparedEffect {
    using Kind = EffectKind;
    Kind kind = Kind::skew;
    bool enabled = false;
    std::optional<EffectRange> range;
    static constexpr std::size_t maximumParameters = 6;
    // Which copy of what it is on this is, for an effect that makes copies.
    int copy = 0;
    std::array<Curve, maximumParameters> curves;
    std::array<double, maximumParameters> minimum {}, maximum {};
    std::size_t count = 0;

    explicit PreparedEffect(const EffectInstance& effect) {
        const auto* definition = effectDefinition(effect.type);
        if (!effect.valid() || definition == nullptr) {
            return;
        }
        kind = definition->kind;
        enabled = effect.enabled;
        range = effect.range;
        count = std::min(definition->parameters.size(), maximumParameters);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& parameter = definition->parameters[index];
            curves[index] = effect.properties.at(parameter.id);
            minimum[index] = parameter.min;
            maximum[index] = parameter.max;
        }
    }

    // The parameters at a time, or nothing when the effect does not act then
    // (bypassed, outside its range, at zero strength or not finite).
    struct Parameters {
        bool active = false;
        std::array<double, maximumParameters> values {};
    };
    Parameters parametersAt(double time) const {
        Parameters result;
        if (!enabled || !std::isfinite(time) || (range.has_value() && (time < range->start || time >= range->end()))) {
            return result;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const auto value = curves[index].evaluate(time);
            if (!std::isfinite(value)) {
                return result;
            }
            result.values[index] = std::clamp(value, minimum[index], maximum[index]);
        }
        result.active = result.values[0] > 0;
        return result;
    }

    osci::Point apply(osci::Point input, double time) const {
        const auto& parameters = cached.at(time, [&] { return parametersAt(time); });
        if (!parameters.active) {
            return input;
        }
        const auto& values = parameters.values;
        const auto strength = values[0];
        auto output = input;
        using namespace osci::point_effects;
        switch (kind) {
            case Kind::skew: output = skew(input, values[1], values[2], values[3]); break;
            case Kind::swirl: output = swirl(input, values[1]); break;
            case Kind::bulge: output = bulge(input, values[1]); break;
            case Kind::ripple: output = ripple(input, values[1], values[2], values[3]); break;
            case Kind::colour: output = colour(input, values[1], values[2], values[3]); break;
            // Strength mixes every effect below, so the kernel runs at full strength.
            case Kind::vortex: output = vortex(input, 1, values[1], values[2]); break;
            case Kind::bitCrush: output = bitCrush(input, values[1]); break;
            case Kind::twist: output = twist(input, values[1]); break;
            case Kind::polygon: output = polygon(input, values[1], values[2], values[3], values[4]); break;
            case Kind::spiralCrush: output = spiralCrush(input, values[1], values[2], values[3], values[4]); break;
            case Kind::kaleidoscope: output = copies::kaleidoscope(input, copy, std::round(values[1]), values[2], values[3], values[4], values[5]); break;
            case Kind::duplicator: output = copies::duplicate(input, copy, std::round(values[1]), values[2], values[3]); break;
        }
        output = ((1 - strength) * input + strength * output).withColour(
            input.r + strength * (output.r - input.r), input.g + strength * (output.g - input.g), input.b + strength * (output.b - input.b));
        if (!output.isFinite()) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return output;
    }

    bool makesCopies() const { return motion::makesCopies(kind); }
    // How many copies it makes at a time: one while it doesn't act.
    int copiesAt(double time) const {
        if (!makesCopies()) { return 1; }
        const auto& parameters = cached.at(time, [&] { return parametersAt(time); });
        return parameters.active ? std::clamp(static_cast<int>(std::round(parameters.values[1])), 1, maximumCopies) : 1;
    }
    // Whether its copy draws at a time: those past the count it makes then don't.
    bool shows(double time) const { return !makesCopies() || copy < copiesAt(time); }
    // The most copies it can make: its count, or (keyed, modulated or linked)
    // the most the parameter allows.
    int mostCopies() const {
        if (!makesCopies() || count < 2) { return 1; }
        const auto& counted = curves[1];
        // Keyed alone, the most its keys reach; linked or modulated, the most it may.
        auto top = maximum[1];
        if (!counted.linked() && (counted.drivers == nullptr || counted.drivers->routes.empty())) {
            top = counted.base;
            for (const auto& key : counted.keyframes()) { top = std::max(top, key.value); }
            top = std::ceil(top - 1.0e-9);
        }
        return std::clamp(static_cast<int>(top), 1, maximumCopies);
    }

private:
    TimeCache<Parameters> cached;
};

inline std::vector<PreparedEffect> prepareEffects(const std::vector<EffectInstance>& effects) {
    std::vector<PreparedEffect> prepared;
    prepared.reserve(effects.size());
    for (const auto& effect : effects) {
        prepared.emplace_back(effect);
    }
    return prepared;
}

inline osci::Point applyEffects(const std::vector<PreparedEffect>& effects, osci::Point point, double time) {
    for (const auto& effect : effects) {
        point = effect.apply(point, time);
    }
    return point;
}
}
