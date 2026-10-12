#pragma once

#include "Animation.h"
#include <array>
#include <map>
#include <string_view>

namespace motion {
// Every animatable property's name, range, default and presentation: the one
// source for what targets hold, what loads as valid, and how the inspector,
// Graph and timeline show and edit values.
struct PropertySpec {
    std::string_view id, label, group, axis;
    double minimum, maximum, defaultValue, step;
    int decimals;
    std::string_view unit;

    double clamp(double value) const { return std::clamp(value, minimum, maximum); }
    bool contains(double value) const { return std::isfinite(value) && value >= minimum && value <= maximum; }
};

inline constexpr double unbounded = 1.0e6;
// 2 atan(1/4): a 0.5-wide frame fills the view at the default distance of 4.
// The most one object's beam time can be scaled by.
inline constexpr double maximumBeam = 16;
inline constexpr double defaultCameraFieldOfView = 28.072486935852954;

// Clips (visual) and groups. The anchor is the point (in the object's own
// space) that rotation and scale turn about, and that lands on the position,
// as After Effects' anchor point does. Beam scales the object's share of the
// beam's time: 2x draws it twice as slowly (brighter on a scope) and takes the
// time from the others; 0 leaves it out. Lit by lights is how far lights light
// it: at 0 it draws in its own colour whatever lights shine, as a flame or a
// screen gives off its own light. Reflects is how brightly its surfaces show
// the rest of the scene, as a mirror does: at 100 a perfect mirror. A group's
// makes what it holds reflect at least as much. The Lines properties are how
// a surface draws itself: its own lines (Edges), its outline as the camera
// sees it, and patterns over it that show its shape, each as bright as set
// (Fill detail is how many lines across a pattern draws).
inline constexpr auto objectPropertySpecs = std::to_array<PropertySpec>({
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
    {"opacity", "Opacity", "Opacity", "", 0, 100, 100, 1, 0, "%"},
    {"anchor.x", "Anchor X", "Anchor", "X", -unbounded, unbounded, 0, .01, 3, ""},
    {"anchor.y", "Anchor Y", "Anchor", "Y", -unbounded, unbounded, 0, .01, 3, ""},
    {"anchor.z", "Anchor Z", "Anchor", "Z", -unbounded, unbounded, 0, .01, 3, ""},
    {"beam", "Beam", "Beam", "", 0, maximumBeam, 1, .01, 2, "×"},
    {"lit", "Lit by lights", "Lit by lights", "", 0, 100, 100, 1, 0, "%"},
    {"reflects", "Mirror", "Mirror", "", 0, 100, 0, 1, 0, "%"},
    {"edges", "Edges", "Edges", "", 0, 100, 100, 1, 0, "%"},
    {"outline", "Outline", "Outline", "", 0, 100, 0, 1, 0, "%"},
    {"spiral", "Spiral", "Spiral", "", 0, 100, 0, 1, 0, "%"},
    {"noise", "Noise", "Noise", "", 0, 100, 0, 1, 0, "%"},
    {"slices", "Slices", "Slices", "", 0, 100, 0, 1, 0, "%"},
    {"rings", "Rings", "Rings", "", 0, 100, 0, 1, 0, "%"},
    {"fillDetail", "Fill detail", "Fill detail", "", 2, 64, 8, 1, 0, ""},
    {"noisePhase", "Noise phase", "Noise phase", "", -unbounded, unbounded, 0, 1, 0, "°"},
    {"noiseFlow", "Noise flow", "Noise flow", "", -10, 10, 0, .01, 2, "/s"},
});
// Where a property is in a list of them, found at compile time: a property
// renamed or gone fails the build rather than reading another.
template <std::size_t size>
consteval std::size_t specIndex(const std::array<PropertySpec, size>& specs, std::string_view id) {
    for (std::size_t index = 0; index < size; ++index) {
        if (specs[index].id == id) { return index; }
    }
    throw "No such property";
}
inline constexpr auto opacityIndex = specIndex(objectPropertySpecs, "opacity"), anchorIndex = specIndex(objectPropertySpecs, "anchor.x"), beamIndex = specIndex(objectPropertySpecs, "beam"),
                      litIndex = specIndex(objectPropertySpecs, "lit"), reflectsIndex = specIndex(objectPropertySpecs, "reflects");
// The Lines properties: edges, outline, the four patterns (in Pattern order:
// spiral, noise, slices, rings), the fill's detail, and how
// the noise morphs: where it is in its loop, and how fast it goes round.
inline constexpr auto edgesIndex = specIndex(objectPropertySpecs, "edges"), outlineIndex = specIndex(objectPropertySpecs, "outline"), patternsIndex = specIndex(objectPropertySpecs, "spiral"),
                      fillDetailIndex = specIndex(objectPropertySpecs, "fillDetail"), noisePhaseIndex = specIndex(objectPropertySpecs, "noisePhase"), noiseFlowIndex = specIndex(objectPropertySpecs, "noiseFlow");
inline constexpr std::size_t patternCount = fillDetailIndex - patternsIndex;
inline bool isLinesProperty(std::string_view id) {
    for (auto index = edgesIndex; index <= noiseFlowIndex; ++index) {
        if (objectPropertySpecs[index].id == id) { return true; }
    }
    return false;
}

// The same properties as Properties shows them: the anchor closes Transform,
// and Reflects closes Surface, after how the surface draws itself.
inline constexpr auto objectInspectorSpecs = [] {
    constexpr auto order = std::to_array<std::size_t>({0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 9, 10, 11, 12, 17, 16, 19, 20, 21, 22, 23, 24, 25, 26, 27, 18});
    static_assert(order.size() == objectPropertySpecs.size(), "every property, once");
    std::array<PropertySpec, order.size()> result {};
    for (std::size_t index = 0; index < order.size(); ++index) { result[index] = objectPropertySpecs[order[index]]; }
    return result;
}();

inline constexpr auto audioPropertySpecs = std::to_array<PropertySpec>({
    {"gain", "Gain", "Gain", "", 0, 4, 1, .01, 2, ""},
    {"pan", "Pan", "Pan", "", -1, 1, 0, .01, 2, ""},
});

// A Lua source's sliders (slider_a .. slider_z in the script), 0..1, animated
// per clip. Changes re-bake that clip's frames in the background.
inline constexpr auto luaSliderSpecs = std::to_array<PropertySpec>({
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
});

// Hatching: the scene shaded by a Hilbert curve over the frame, denser where
// more light falls, rather than by brightness. Hatch detail is how many of
// its cells span the frame where the light is full.
inline constexpr auto cameraPropertySpecs = std::to_array<PropertySpec>({
    {"position.x", "Position X", "Position", "X", -unbounded, unbounded, 0, .01, 3, ""},
    {"position.y", "Position Y", "Position", "Y", -unbounded, unbounded, 0, .01, 3, ""},
    {"position.z", "Position Z", "Position", "Z", -unbounded, unbounded, 4, .01, 3, ""},
    {"rotation.x", "Rotation X", "Rotation", "X", -unbounded, unbounded, 0, 1, 1, "°"},
    {"rotation.y", "Rotation Y", "Rotation", "Y", -unbounded, unbounded, 0, 1, 1, "°"},
    {"rotation.z", "Rotation Z", "Rotation", "Z", -unbounded, unbounded, 0, 1, 1, "°"},
    {"fov", "Field of view", "Field of view", "", 1, 179, defaultCameraFieldOfView, .5, 1, "°"},
    {"hatching", "Hatching", "Hatching", "", 0, 100, 0, 1, 0, "%"},
    {"hatchDetail", "Hatch detail", "Hatch detail", "", 8, 256, 64, 1, 0, ""},
});
inline constexpr auto cameraHatchingIndex = specIndex(cameraPropertySpecs, "hatching"), cameraHatchDetailIndex = specIndex(cameraPropertySpecs, "hatchDetail");

// The Scope's picture: one row each, in the visualiser's own ranges.
inline constexpr auto beamPropertySpecs = std::to_array<PropertySpec>({
    {"intensity", "Intensity", "Intensity", "", 0, 50, 5, .02, 2, ""},
    {"focus", "Focus", "Focus", "", .3, 10, 1, .02, 2, ""},
    {"persistence", "Persistence", "Persistence", "", 0, 6, .5, .01, 2, ""},
    {"afterglow", "Afterglow", "Afterglow", "", 0, 10, 1, .02, 2, ""},
    {"glow", "Glow", "Glow", "", 0, 1, .3, .005, 2, ""},
    {"hue", "Hue", "Hue", "", 0, 359, 125, 1, 0, "°"},
    {"lineSaturation", "Saturation", "Saturation", "", 0, 5, 1, .01, 2, ""},
    {"ambient", "Ambient", "Ambient", "", 0, 5, 0, .01, 2, ""},
    {"visualiserSmoothing", "Smoothing", "Smoothing", "", 0, 1, 0, .005, 2, ""},
});

// A new target's properties: each spec's default, unanimated.
template <std::size_t size>
PropertyMap defaultProperties(const std::array<PropertySpec, size>& specs) {
    PropertyMap properties;
    for (const auto& spec : specs) { properties.emplace(spec.id, Curve(spec.defaultValue)); }
    return properties;
}

// A target's curve for a spec, or the spec's default when it has none.
inline Curve curveOrDefault(const PropertyMap& properties, const PropertySpec& spec) {
    const auto found = properties.find(spec.id);
    return found != properties.end() ? found->second : Curve(spec.defaultValue);
}

// Exactly these specs' properties, each valid and with every value in range.
template <std::size_t size>
bool validProperties(const PropertyMap& properties, const std::array<PropertySpec, size>& specs) {
    if (properties.size() != specs.size()) { return false; }
    for (const auto& spec : specs) {
        const auto found = properties.find(spec.id);
        if (found == properties.end() || !found->second.valid() || !spec.contains(found->second.base)) { return false; }
        for (const auto& key : found->second.keyframes()) {
            if (!spec.contains(key.value)) { return false; }
        }
    }
    return true;
}
}
