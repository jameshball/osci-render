#pragma once

#include "PropertyTarget.h"
#include <span>
#include <string_view>

namespace motion {
// Presentation and editing rules for every animatable property: one source for
// labels, ranges, units and precision in the inspector, graph and timeline.
struct PropertySpec {
    std::string_view id, label, group, axis;
    double minimum, maximum, fallback, step;
    int decimals;
    std::string_view unit;

    double clamp(double value) const { return std::clamp(value, minimum, maximum); }
};

inline constexpr double unbounded = 1.0e6;

inline std::span<const PropertySpec> objectPropertySpecs() {
    static constexpr PropertySpec specs[] {
        {"position.x", "Position X", "Position", "X", -unbounded, unbounded, 0, .01, 3, ""},
        {"position.y", "Position Y", "Position", "Y", -unbounded, unbounded, 0, .01, 3, ""},
        {"position.z", "Position Z", "Position", "Z", -unbounded, unbounded, 0, .01, 3, ""},
        {"rotation.x", "Rotation X", "Rotation", "X", -unbounded, unbounded, 0, 1, 1, "°"},
        {"rotation.y", "Rotation Y", "Rotation", "Y", -unbounded, unbounded, 0, 1, 1, "°"},
        {"rotation.z", "Rotation Z", "Rotation", "Z", -unbounded, unbounded, 0, 1, 1, "°"},
        {"scale.x", "Scale X", "Scale", "X", -unbounded, unbounded, 1, .01, 3, ""},
        {"scale.y", "Scale Y", "Scale", "Y", -unbounded, unbounded, 1, .01, 3, ""},
        {"scale.z", "Scale Z", "Scale", "Z", -unbounded, unbounded, 1, .01, 3, ""},
        {"red", "Red", "Colour", "R", 0, 1, 1, .01, 2, ""},
        {"green", "Green", "Colour", "G", 0, 1, 1, .01, 2, ""},
        {"blue", "Blue", "Colour", "B", 0, 1, 1, .01, 2, ""},
        {"weight", "Drawing weight", "Drawing", "", 0, unbounded, 1, .01, 2, ""},
    };
    return specs;
}

inline std::span<const PropertySpec> audioPropertySpecs() {
    static constexpr PropertySpec specs[] {
        {"gain", "Gain", "Gain", "", 0, 4, 1, .01, 2, ""},
        {"pan", "Pan", "Pan", "", -1, 1, 0, .01, 2, ""},
    };
    return specs;
}

inline std::span<const PropertySpec> cameraPropertySpecs() {
    static constexpr PropertySpec specs[] {
        {"position.x", "Position X", "Position", "X", -unbounded, unbounded, 0, .01, 3, ""},
        {"position.y", "Position Y", "Position", "Y", -unbounded, unbounded, 0, .01, 3, ""},
        {"position.z", "Position Z", "Position", "Z", -unbounded, unbounded, 4, .01, 3, ""},
        {"rotation.x", "Rotation X", "Rotation", "X", -unbounded, unbounded, 0, 1, 1, "°"},
        {"rotation.y", "Rotation Y", "Rotation", "Y", -unbounded, unbounded, 0, 1, 1, "°"},
        {"rotation.z", "Rotation Z", "Rotation", "Z", -unbounded, unbounded, 0, 1, 1, "°"},
        {"fov", "Field of view", "Lens", "", 1, 179, 28.07, .5, 1, "°"},
    };
    return specs;
}

inline std::span<const PropertySpec> propertySpecs(const PropertyTarget& target) {
    if (target.camera) { return cameraPropertySpecs(); }
    if (target.isAudio) { return audioPropertySpecs(); }
    return objectPropertySpecs();
}

inline const PropertySpec* findPropertySpec(std::span<const PropertySpec> specs, std::string_view id) {
    for (const auto& spec : specs) {
        if (spec.id == id) { return &spec; }
    }
    return nullptr;
}

// Effects declare their own parameter ranges in the catalogue.
inline PropertySpec effectPropertySpec(const EffectParameterDefinition& parameter) {
    const auto span = parameter.max - parameter.min;
    return {parameter.id, parameter.name, "Effect", "", parameter.min, parameter.max, parameter.defaultValue, span / 200.0, span >= 50 ? 1 : 3, ""};
}
}
