#pragma once

#include "../model/Effects.h"
#include "TimeCache.h"
#include "../../../modules/osci_render_core/effects/osci_PointEffectKernels.h"
#include <array>
#include <cmath>
#include <numbers>

namespace motion {
struct PreparedEffect {
    using Kind = EffectKind;
    Kind kind = Kind::rotate;
    bool enabled = false;
    std::optional<EffectRange> range;
    static constexpr std::size_t maximumParameters = 5;
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
        // Wobble's offset, which depends only on the time.
        float wobble = 0;
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
        if (kind == Kind::wobble) { result.wobble = static_cast<float>(.5 * result.values[1] * std::sin((time * result.values[2] + result.values[3]) * 2 * std::numbers::pi)); }
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
            case Kind::rotate: output = rotate(input, values[1], values[2], values[3]); break;
            case Kind::scale: output = scale(input, values[1], values[2], values[3]); break;
            case Kind::translate: output = translate(input, values[1], values[2], values[3]); break;
            case Kind::skew: output = skew(input, values[1], values[2], values[3]); break;
            case Kind::swirl: output = swirl(input, values[1]); break;
            case Kind::bulge: output = bulge(input, values[1]); break;
            case Kind::ripple: output = ripple(input, values[1], values[2], values[3]); break;
            case Kind::colour: output = colour(input, values[1], values[2], values[3]); break;
            case Kind::vortex: output = vortex(input, values[1], values[2], values[3]); break;
            case Kind::bitCrush: output = bitCrush(input, values[1]); break;
            case Kind::twist: output = twist(input, values[1]); break;
            case Kind::polygon: output = polygon(input, values[1], values[2], values[3], values[4]); break;
            case Kind::spiralCrush: output = spiralCrush(input, values[1], values[2], values[3], values[4]); break;
            case Kind::perspective: output = perspective(input, values[1]); break;
            case Kind::wobble: {
                const auto delta = parameters.wobble;
                output = osci::Point(input.x + delta, input.y + delta, input.z + delta).withColour(input.r, input.g, input.b);
                break;
            }
        }
        output = ((1 - strength) * input + strength * output).withColour(
            input.r + strength * (output.r - input.r), input.g + strength * (output.g - input.g), input.b + strength * (output.b - input.b));
        if (!output.isFinite()) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return output;
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
