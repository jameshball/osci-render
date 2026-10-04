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

// A Lua source's sliders (slider_a .. slider_z in the script), 0..1, animated
// per clip. Changes re-bake that clip's frames in the background.
inline std::span<const PropertySpec> luaSliderSpecs() {
    static constexpr PropertySpec specs[] {
        {"slider.a", "Slider A", "Slider A", "", 0, 1, 0, .01, 3, ""}, {"slider.b", "Slider B", "Slider B", "", 0, 1, 0, .01, 3, ""},
        {"slider.c", "Slider C", "Slider C", "", 0, 1, 0, .01, 3, ""}, {"slider.d", "Slider D", "Slider D", "", 0, 1, 0, .01, 3, ""},
        {"slider.e", "Slider E", "Slider E", "", 0, 1, 0, .01, 3, ""}, {"slider.f", "Slider F", "Slider F", "", 0, 1, 0, .01, 3, ""},
        {"slider.g", "Slider G", "Slider G", "", 0, 1, 0, .01, 3, ""}, {"slider.h", "Slider H", "Slider H", "", 0, 1, 0, .01, 3, ""},
        {"slider.i", "Slider I", "Slider I", "", 0, 1, 0, .01, 3, ""}, {"slider.j", "Slider J", "Slider J", "", 0, 1, 0, .01, 3, ""},
        {"slider.k", "Slider K", "Slider K", "", 0, 1, 0, .01, 3, ""}, {"slider.l", "Slider L", "Slider L", "", 0, 1, 0, .01, 3, ""},
        {"slider.m", "Slider M", "Slider M", "", 0, 1, 0, .01, 3, ""}, {"slider.n", "Slider N", "Slider N", "", 0, 1, 0, .01, 3, ""},
        {"slider.o", "Slider O", "Slider O", "", 0, 1, 0, .01, 3, ""}, {"slider.p", "Slider P", "Slider P", "", 0, 1, 0, .01, 3, ""},
        {"slider.q", "Slider Q", "Slider Q", "", 0, 1, 0, .01, 3, ""}, {"slider.r", "Slider R", "Slider R", "", 0, 1, 0, .01, 3, ""},
        {"slider.s", "Slider S", "Slider S", "", 0, 1, 0, .01, 3, ""}, {"slider.t", "Slider T", "Slider T", "", 0, 1, 0, .01, 3, ""},
        {"slider.u", "Slider U", "Slider U", "", 0, 1, 0, .01, 3, ""}, {"slider.v", "Slider V", "Slider V", "", 0, 1, 0, .01, 3, ""},
        {"slider.w", "Slider W", "Slider W", "", 0, 1, 0, .01, 3, ""}, {"slider.x", "Slider X", "Slider X", "", 0, 1, 0, .01, 3, ""},
        {"slider.y", "Slider Y", "Slider Y", "", 0, 1, 0, .01, 3, ""}, {"slider.z", "Slider Z", "Slider Z", "", 0, 1, 0, .01, 3, ""},
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

// The Scope's picture: one row each, in the visualiser's own ranges.
inline std::span<const PropertySpec> beamPropertySpecs() {
    static constexpr PropertySpec specs[] {
        {"intensity", "Intensity", "Intensity", "", 0, 10, 5, .02, 2, ""},
        {"focus", "Focus", "Focus", "", .3, 10, 1, .02, 2, ""},
        {"persistence", "Persistence", "Persistence", "", 0, 6, .5, .01, 2, ""},
        {"afterglow", "Afterglow", "Afterglow", "", 0, 10, 1, .02, 2, ""},
        {"glow", "Glow", "Glow", "", 0, 1, .3, .005, 2, ""},
        {"hue", "Hue", "Hue", "", 0, 359, 125, 1, 0, "°"},
        {"lineSaturation", "Saturation", "Saturation", "", 0, 5, 1, .01, 2, ""},
        {"ambient", "Ambient", "Ambient", "", 0, 5, 0, .01, 2, ""},
        {"visualiserSmoothing", "Smoothing", "Smoothing", "", 0, 1, 0, .005, 2, ""},
    };
    return specs;
}

inline std::span<const PropertySpec> propertySpecs(const PropertyTarget& target) {
    if (target.beam) { return beamPropertySpecs(); }
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
