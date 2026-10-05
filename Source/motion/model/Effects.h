#pragma once

#include "Animation.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace motion {
inline constexpr std::size_t maximumEffectsPerOwner = 64;
struct EffectParameterDefinition {
    std::string id, name;
    double defaultValue, min, max;
};

// What an effect does to each point; prepared effects switch on it.
enum class EffectKind { rotate, scale, translate, skew, swirl, bulge, ripple, vortex, colour, bitCrush, twist, polygon, spiralCrush, perspective, wobble };

struct EffectDefinition {
    EffectKind kind;
    std::string id, name;
    std::vector<EffectParameterDefinition> parameters;
};

inline const std::vector<EffectDefinition>& effectCatalog() {
    static const std::vector<EffectDefinition> catalog {
        { EffectKind::rotate, "rotate", "Rotate", {{"strength", "Strength", 1, 0, 1}, {"rotateX", "Rotate X", 0, -1, 1}, {"rotateY", "Rotate Y", 0, -1, 1}, {"rotateZ", "Rotate Z", 0, -1, 1}} },
        { EffectKind::scale, "scale", "Scale", {{"strength", "Strength", 1, 0, 1}, {"scaleX", "Scale X", 1.2, -3, 3}, {"scaleY", "Scale Y", 1.2, -3, 3}, {"scaleZ", "Scale Z", 1.2, -3, 3}} },
        { EffectKind::translate, "translate", "Translate", {{"strength", "Strength", 1, 0, 1}, {"translateX", "Translate X", 0.3, -1, 1}, {"translateY", "Translate Y", 0, -1, 1}, {"translateZ", "Translate Z", 0, -1, 1}} },
        { EffectKind::skew, "skew", "Skew", {{"strength", "Strength", 1, 0, 1}, {"skewX", "Skew X", 0, -1, 1}, {"skewY", "Skew Y", 0, -1, 1}, {"skewZ", "Skew Z", 0, -1, 1}} },
        { EffectKind::swirl, "swirl", "Swirl", {{"strength", "Strength", 1, 0, 1}, {"swirl", "Swirl", 0.4, -1, 1}} },
        { EffectKind::bulge, "bulge", "Bulge", {{"strength", "Strength", 1, 0, 1}, {"bulge", "Bulge", 0.5, 0, 1}} },
        { EffectKind::ripple, "ripple", "Ripple", {{"strength", "Strength", 1, 0, 1}, {"rippleDepth", "Depth", 0.2, 0, 1}, {"ripplePhase", "Phase", 0, -1, 1}, {"rippleAmount", "Amount", 0.1, 0, 1}} },
        { EffectKind::vortex, "vortex", "Vortex", {{"strength", "Strength", 1, 0, 1}, {"vortexStrength", "Vortex strength", 0.6, 0, 1}, {"vortexAmount", "Amount", 2, 2, 6}, {"vortexRotation", "Rotation", 0.25, 0, 1}} },
        { EffectKind::colour, "colour", "Colour", {{"strength", "Strength", 1, 0, 1}, {"hue", "Hue (degrees)", 0, -180, 180}, {"saturation", "Saturation", 1, 0, 2}, {"brightness", "Brightness", 1, 0, 2}} },
        { EffectKind::bitCrush, "bitCrush", "Bit crush", {{"strength", "Strength", 1, 0, 1}, {"crush", "Crush", 0.7, 0, 1}} },
        { EffectKind::twist, "twist", "Twist", {{"strength", "Strength", 1, 0, 1}, {"twist", "Twist", 0.5, -1, 1}} },
        { EffectKind::polygon, "polygon", "Polygon", {{"strength", "Strength", 1, 0, 1}, {"sides", "Sides", 5, 2, 12}, {"stripes", "Stripe size", 0.5, 0, 1}, {"turn", "Rotation", 0, 0, 1}, {"stripePhase", "Stripe phase", 0, 0, 1}} },
        { EffectKind::spiralCrush, "spiralCrush", "Spiral crush", {{"strength", "Strength", 1, 0, 1}, {"density", "Density", 13, 3, 30}, {"spiralTwist", "Twist", 0.6, -1, 1}, {"zoom", "Zoom", 0, 0, 1}, {"turn", "Rotation", 0, 0, 1}} },
        { EffectKind::perspective, "perspective", "Perspective", {{"strength", "Strength", 1, 0, 1}, {"fov", "Field of view", 50, 5, 130}} },
        { EffectKind::wobble, "wobble", "Wobble", {{"strength", "Strength", 1, 0, 1}, {"amount", "Amount", 0.3, 0, 1}, {"rate", "Rate (Hz)", 2, 0, 20}, {"phase", "Phase", 0, 0, 1}} }
    };
    return catalog;
}

inline const EffectDefinition* effectDefinition(std::string_view id) {
    for (const auto& definition : effectCatalog()) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

struct EffectRange {
    double start = 0, duration = 1;
    double end() const { return start + duration; }
    bool valid() const { return std::isfinite(start) && std::isfinite(duration) && duration > 0 && std::isfinite(end()); }
};

struct EffectInstance {
    std::uint64_t id = 0;
    std::string type, name;
    bool enabled = true;
    PropertyMap properties;
    std::optional<EffectRange> range;

    bool valid() const {
        const auto* definition = effectDefinition(type);
        if (id == 0 || definition == nullptr || (range.has_value() && !range->valid()) || properties.size() != definition->parameters.size()) {
            return false;
        }
        for (const auto& parameter : definition->parameters) {
            const auto found = properties.find(parameter.id);
            if (found == properties.end() || !found->second.valid()) {
                return false;
            }
            const auto validValue = [&](double value) { return std::isfinite(value) && value >= parameter.min && value <= parameter.max; };
            if (!validValue(found->second.base)) {
                return false;
            }
            for (const auto& key : found->second.keyframes()) {
                if (!validValue(key.value)) {
                    return false;
                }
            }
        }
        return true;
    }
};

inline EffectInstance makeEffect(std::uint64_t id, const EffectDefinition& definition) {
    EffectInstance effect;
    effect.id = id;
    effect.type = definition.id;
    effect.name = definition.name;
    for (const auto& parameter : definition.parameters) {
        effect.properties.emplace(parameter.id, Curve(parameter.defaultValue));
    }
    return effect;
}

template <typename ProjectType>
auto findEffectOwner(ProjectType& project, std::uint64_t ownerId) -> std::conditional_t<std::is_const_v<ProjectType>, const std::vector<EffectInstance>*, std::vector<EffectInstance>*> {
    if (ownerId == 0) {
        return &project.effects;
    }
    for (auto& group : project.groups) {
        if (group.id == ownerId) {
            return &group.effects;
        }
    }
    for (auto& track : project.tracks) {
        if (track.kind == decltype(track.kind)::audio) { continue; }
        if (track.id == ownerId) {
            return &track.effects;
        }
        for (auto& clip : track.clips) {
            if (clip.id == ownerId) {
                return &clip.effects;
            }
        }
    }
    return nullptr;
}

template <typename ProjectType>
auto findEffect(ProjectType& project, std::uint64_t id) -> std::conditional_t<std::is_const_v<ProjectType>, const EffectInstance*, EffectInstance*> {
    for (auto& effect : project.effects) {
        if (effect.id == id) {
            return &effect;
        }
    }
    for (auto& group : project.groups) {
        for (auto& effect : group.effects) {
            if (effect.id == id) {
                return &effect;
            }
        }
    }
    for (auto& track : project.tracks) {
        for (auto& effect : track.effects) {
            if (effect.id == id) {
                return &effect;
            }
        }
        for (auto& clip : track.clips) {
            for (auto& effect : clip.effects) {
                if (effect.id == id) {
                    return &effect;
                }
            }
        }
    }
    return nullptr;
}
}
