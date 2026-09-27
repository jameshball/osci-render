#pragma once

#include "../model/Effects.h"
#include "../../../modules/osci_render_core/effects/osci_PointEffectKernels.h"
#include <array>

namespace motion {
struct PreparedEffect {
    enum class Kind { rotate, scale, translate, skew, swirl, bulge, ripple, vortex, colour };
    Kind kind = Kind::rotate;
    bool enabled = false;
    std::optional<EffectRange> range;
    std::array<Curve, 4> curves;
    std::array<double, 4> minimum {}, maximum {};
    std::size_t count = 0;

    explicit PreparedEffect(const EffectInstance& effect) {
        const auto* definition = effectDefinition(effect.type);
        if (!effect.valid() || definition == nullptr) {
            return;
        }
        const auto& catalog = effectCatalog();
        kind = static_cast<Kind>(definition - catalog.data());
        enabled = effect.enabled;
        range = effect.range;
        count = definition->parameters.size();
        for (std::size_t index = 0; index < count; ++index) {
            const auto& parameter = definition->parameters[index];
            curves[index] = effect.properties.at(parameter.id);
            minimum[index] = parameter.min;
            maximum[index] = parameter.max;
        }
    }

    osci::Point apply(osci::Point input, double time, double bpm = 120) const {
        if (!enabled || !std::isfinite(time) || (range.has_value() && (time < range->start || time >= range->end()))) {
            return input;
        }
        std::array<double, 4> values {};
        for (std::size_t index = 0; index < count; ++index) {
            const auto value = curves[index].evaluate(time, bpm);
            if (!std::isfinite(value)) {
                return input;
            }
            values[index] = std::clamp(value, minimum[index], maximum[index]);
        }
        const auto strength = values[0];
        if (strength <= 0) {
            return input;
        }
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
        }
        output = ((1 - strength) * input + strength * output).withColour(
            input.r + strength * (output.r - input.r), input.g + strength * (output.g - input.g), input.b + strength * (output.b - input.b));
        if (!std::isfinite(output.x) || !std::isfinite(output.y) || !std::isfinite(output.z)
            || !std::isfinite(output.r) || !std::isfinite(output.g) || !std::isfinite(output.b)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return output;
    }
};

inline std::vector<PreparedEffect> prepareEffects(const std::vector<EffectInstance>& effects) {
    std::vector<PreparedEffect> prepared;
    prepared.reserve(effects.size());
    for (const auto& effect : effects) {
        prepared.emplace_back(effect);
    }
    return prepared;
}

inline osci::Point applyEffects(const std::vector<PreparedEffect>& effects, osci::Point point, double time, double bpm = 120) {
    for (const auto& effect : effects) {
        point = effect.apply(point, time, bpm);
    }
    return point;
}
}
