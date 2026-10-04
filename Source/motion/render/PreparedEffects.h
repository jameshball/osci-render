#pragma once

#include "../model/Effects.h"
#include "TimeCache.h"
#include "../../../modules/osci_render_core/effects/osci_PointEffectKernels.h"
#include <array>
#include <cmath>
#include <numbers>

namespace motion {
// osci-render's stateless point effects, as pure functions of the point.
inline osci::Point bitCrush(osci::Point input, double crush) {
    const auto quant = .5 * std::pow(2.0, (std::pow(2.0, 1.0 - crush * .78) - 1.0) * 12);
    return osci::Point(std::round(input.x * quant) / quant, std::round(input.y * quant) / quant, std::round(input.z * quant) / quant).withColour(input.r, input.g, input.b);
}
inline osci::Point twist(osci::Point input, double amount) {
    input.rotate(0.0f, static_cast<float>(amount * 4 * std::numbers::pi * input.y), 0.0f);
    return input;
}
inline osci::Point polygon(osci::Point input, double sides, double stripes, double turn, double phase) {
    constexpr auto pi = std::numbers::pi;
    const auto stripe = std::pow(.63 * std::max(1.0e-4, stripes), 1.5);
    osci::Point output(0);
    if (input.x != 0 || input.y != 0) {
        const auto r = std::hypot(input.x, input.y);
        auto theta = std::atan2(-input.x, input.y) - turn * 2 * pi;
        theta = std::fmod(std::fmod(theta + pi, 2 * pi) + 2 * pi, 2 * pi) - pi;
        const auto centre = std::round(theta * std::max(2.0, sides) / (2 * pi)) / std::max(2.0, sides) * 2 * pi;
        const auto distance = r * std::cos(theta - centre);
        const auto next = std::max(0.0, (std::round(distance / stripe - phase) + phase) * stripe);
        const auto ratio = distance != 0 ? next / distance : 0.0;
        output.x = static_cast<float>(ratio * input.x);
        output.y = static_cast<float>(ratio * input.y);
    }
    if (std::abs(input.z) > 1.0e-4f) { output.z = static_cast<float>((input.z > 0 ? 1 : -1) * std::max(0.0, (std::round(std::abs(input.z) / stripe - phase) + phase) * stripe)); }
    return output.withColour(input.r, input.g, input.b);
}
inline osci::Point spiralCrush(osci::Point input, double density, double spiralTwist, double zoomAmount, double turn) {
    constexpr auto pi = std::numbers::pi;
    const auto domainX = std::max(2.0, std::floor(density + .001));
    const auto domainY = std::round(domainX * spiralTwist);
    const auto zoom = zoomAmount * 2 * pi, rotation = turn * 2 * pi;
    const auto scale = std::hypot(domainX, domainY) / (2 * pi);
    const auto angle = std::atan2(domainY, domainX);
    osci::Point output(0);
    if (input.x != 0 || input.y != 0) {
        osci::Point cell(static_cast<float>(std::atan2(input.x, -input.y) - rotation), static_cast<float>(std::log(std::hypot(input.x, input.y)) - zoom));
        cell.rotate(0, 0, static_cast<float>(angle));
        cell = cell * static_cast<float>(scale);
        cell.x = std::round(cell.x);
        cell.y = std::round(cell.y);
        cell = cell / static_cast<float>(scale);
        cell.rotate(0, 0, static_cast<float>(-angle));
        const auto radius = std::exp(cell.y + zoom), theta = cell.x + rotation;
        output.x = static_cast<float>(radius * std::sin(theta));
        output.y = static_cast<float>(-radius * std::cos(theta));
    }
    if (input.z != 0) {
        const auto logZ = std::round((std::log(std::abs(input.z)) - zoom) * scale) / scale + zoom;
        output.z = static_cast<float>((input.z > 0 ? 1 : -1) * std::exp(logZ));
    }
    return output.withColour(input.r, input.g, input.b);
}
// A pinhole view whose cone just touches the unit sphere: depth shrinks the
// far side and swells the near.
inline osci::Point perspective(osci::Point input, double fovDegrees) {
    const auto fov = std::clamp(fovDegrees, 1.5, 179.0) * std::numbers::pi / 180;
    const auto distance = 1.0 / std::sin(fov * .5);
    const auto depth = distance + input.z;
    if (depth <= 1.0e-3) { return osci::Point(0).withColour(input.r, input.g, input.b); }
    const auto focal = 1.0 / std::tan(fov * .5);
    const auto factor = focal / depth;
    return osci::Point(static_cast<float>(input.x * factor), static_cast<float>(input.y * factor), 0).withColour(input.r, input.g, input.b);
}

struct PreparedEffect {
    // In catalogue order.
    enum class Kind { rotate, scale, translate, skew, swirl, bulge, ripple, vortex, colour, bitCrush, twist, polygon, spiralCrush, perspective, wobble };
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
        const auto& catalog = effectCatalog();
        kind = static_cast<Kind>(definition - catalog.data());
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
    Parameters parametersAt(double time, double bpm) const {
        Parameters result;
        if (!enabled || !std::isfinite(time) || (range.has_value() && (time < range->start || time >= range->end()))) {
            return result;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const auto value = curves[index].evaluate(time, bpm);
            if (!std::isfinite(value)) {
                return result;
            }
            result.values[index] = std::clamp(value, minimum[index], maximum[index]);
        }
        result.active = result.values[0] > 0;
        if (kind == Kind::wobble) { result.wobble = static_cast<float>(.5 * result.values[1] * std::sin((time * result.values[2] + result.values[3]) * 2 * std::numbers::pi)); }
        return result;
    }

    osci::Point apply(osci::Point input, double time, double bpm = 120) const {
        const auto& parameters = cached.at(time, [&] { return parametersAt(time, bpm); });
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
        if (!std::isfinite(output.x) || !std::isfinite(output.y) || !std::isfinite(output.z)
            || !std::isfinite(output.r) || !std::isfinite(output.g) || !std::isfinite(output.b)) {
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

inline osci::Point applyEffects(const std::vector<PreparedEffect>& effects, osci::Point point, double time, double bpm = 120) {
    for (const auto& effect : effects) {
        point = effect.apply(point, time, bpm);
    }
    return point;
}
}
