#pragma once

#include "PropertySpecs.h"
#include <span>
#include <string_view>

namespace motion {
// Sources made from a few numbers rather than a file: shapes, curves, grids
// and solids, and lights. A generator's asset names its kind; each clip of it
// keeps the numbers as its own properties ("generator.points" and so on), so
// they key, modulate and link like any other, and the clip is redrawn from
// them frame by frame. A light draws nothing on the Scope: it lights what
// does, and its drawing is only its picture in the Scene.
enum class GeneratorKind {
    rectangle, ellipse, polygon, star, ring, arc,
    spiral, lissajous, spirograph, rose, wave,
    grid, polarGrid, hexGrid,
    box, sphere, torus, cylinder, cone,
    ambientLight, directionalLight, pointLight, spotLight,
};

// How generators are grouped where they are chosen.
enum class GeneratorCategory { shape, curve, grid, solid, light };

struct GeneratorDefinition {
    GeneratorKind kind;
    std::string_view id, name;
    GeneratorCategory category;
    std::span<const PropertySpec> parameters;
};

namespace generator_specs {
// Sizes are in the frame's units (it spans -1 to 1), angles in degrees, and
// counts are whole numbers (step 1, no decimals).
inline constexpr std::array<PropertySpec, 3> rectangle {{
    {"generator.width", "Width", "Width", "", 0, 4, 1, .01, 3, ""},
    {"generator.height", "Height", "Height", "", 0, 4, 1, .01, 3, ""},
    {"generator.roundness", "Roundness", "Roundness", "", 0, 100, 0, 1, 0, "%"},
}};
inline constexpr std::array<PropertySpec, 2> ellipse {{
    {"generator.width", "Width", "Width", "", 0, 4, 1, .01, 3, ""},
    {"generator.height", "Height", "Height", "", 0, 4, 1, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> polygon {{
    {"generator.sides", "Sides", "Sides", "", 3, 64, 6, 1, 0, ""},
    {"generator.radius", "Radius", "Radius", "", 0, 2, .5, .01, 3, ""},
    {"generator.roundness", "Roundness", "Roundness", "", 0, 100, 0, 1, 0, "%"},
}};
inline constexpr std::array<PropertySpec, 4> star {{
    {"generator.points", "Points", "Points", "", 3, 64, 5, 1, 0, ""},
    {"generator.radius", "Outer radius", "Outer radius", "", 0, 2, .5, .01, 3, ""},
    {"generator.innerRadius", "Inner radius", "Inner radius", "", 0, 2, .2, .01, 3, ""},
    {"generator.roundness", "Roundness", "Roundness", "", 0, 100, 0, 1, 0, "%"},
}};
inline constexpr std::array<PropertySpec, 2> ring {{
    {"generator.radius", "Outer radius", "Outer radius", "", 0, 2, .5, .01, 3, ""},
    {"generator.innerRadius", "Inner radius", "Inner radius", "", 0, 2, .3, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> arc {{
    {"generator.radius", "Radius", "Radius", "", 0, 2, .5, .01, 3, ""},
    {"generator.start", "Start", "Start", "", -360, 360, 0, 1, 1, "°"},
    {"generator.sweep", "Sweep", "Sweep", "", 0, 360, 270, 1, 1, "°"},
}};
inline constexpr std::array<PropertySpec, 3> spiral {{
    {"generator.turns", "Turns", "Turns", "", 0, 32, 4, .01, 2, ""},
    {"generator.innerRadius", "Inner radius", "Inner radius", "", 0, 2, 0, .01, 3, ""},
    {"generator.radius", "Outer radius", "Outer radius", "", 0, 2, .5, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 4> lissajous {{
    {"generator.frequencyX", "Frequency X", "Frequency X", "", 1, 32, 3, 1, 0, ""},
    {"generator.frequencyY", "Frequency Y", "Frequency Y", "", 1, 32, 2, 1, 0, ""},
    {"generator.phase", "Phase", "Phase", "", -360, 360, 90, 1, 1, "°"},
    {"generator.radius", "Size", "Size", "", 0, 2, .5, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> spirograph {{
    {"generator.teeth", "Gear teeth", "Gear teeth", "", 1, 95, 36, 1, 0, ""},
    {"generator.pen", "Pen", "Pen", "", 0, 100, 80, 1, 0, "%"},
    {"generator.radius", "Size", "Size", "", 0, 2, .5, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 2> rose {{
    {"generator.petals", "Petals", "Petals", "", 1, 24, 5, 1, 0, ""},
    {"generator.radius", "Size", "Size", "", 0, 2, .5, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 4> wave {{
    {"generator.cycles", "Cycles", "Cycles", "", 0, 64, 3, .01, 2, ""},
    {"generator.amplitude", "Amplitude", "Amplitude", "", 0, 2, .25, .01, 3, ""},
    {"generator.width", "Length", "Length", "", 0, 4, 1.6, .01, 3, ""},
    {"generator.phase", "Phase", "Phase", "", -360, 360, 0, 1, 1, "°"},
}};
inline constexpr std::array<PropertySpec, 4> grid {{
    {"generator.columns", "Columns", "Columns", "", 1, 64, 8, 1, 0, ""},
    {"generator.rows", "Rows", "Rows", "", 1, 64, 8, 1, 0, ""},
    {"generator.width", "Width", "Width", "", 0, 4, 1.6, .01, 3, ""},
    {"generator.height", "Height", "Height", "", 0, 4, 1.6, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> polarGrid {{
    {"generator.rings", "Rings", "Rings", "", 1, 32, 4, 1, 0, ""},
    {"generator.spokes", "Spokes", "Spokes", "", 0, 64, 12, 1, 0, ""},
    {"generator.radius", "Radius", "Radius", "", 0, 2, .8, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> hexGrid {{
    {"generator.columns", "Columns", "Columns", "", 1, 32, 6, 1, 0, ""},
    {"generator.rows", "Rows", "Rows", "", 1, 32, 5, 1, 0, ""},
    {"generator.radius", "Cell size", "Cell size", "", 0, 1, .15, .005, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> box {{
    {"generator.width", "Width", "Width", "", 0, 4, 1, .01, 3, ""},
    {"generator.height", "Height", "Height", "", 0, 4, 1, .01, 3, ""},
    {"generator.depth", "Depth", "Depth", "", 0, 4, 1, .01, 3, ""},
}};
inline constexpr std::array<PropertySpec, 3> sphere {{
    {"generator.radius", "Radius", "Radius", "", 0, 2, .5, .01, 3, ""},
    {"generator.segments", "Segments", "Segments", "", 3, 64, 16, 1, 0, ""},
    {"generator.rings", "Rings", "Rings", "", 2, 32, 8, 1, 0, ""},
}};
inline constexpr std::array<PropertySpec, 4> torus {{
    {"generator.radius", "Radius", "Radius", "", 0, 2, .4, .01, 3, ""},
    {"generator.thickness", "Thickness", "Thickness", "", 0, 1, .15, .005, 3, ""},
    {"generator.segments", "Segments", "Segments", "", 3, 64, 24, 1, 0, ""},
    {"generator.sides", "Sides", "Sides", "", 3, 32, 10, 1, 0, ""},
}};
inline constexpr std::array<PropertySpec, 3> cylinder {{
    {"generator.radius", "Radius", "Radius", "", 0, 2, .4, .01, 3, ""},
    {"generator.height", "Height", "Height", "", 0, 4, 1, .01, 3, ""},
    {"generator.segments", "Segments", "Segments", "", 3, 64, 16, 1, 0, ""},
}};
inline constexpr std::array<PropertySpec, 3> cone {{
    {"generator.radius", "Radius", "Radius", "", 0, 2, .45, .01, 3, ""},
    {"generator.height", "Height", "Height", "", 0, 4, 1, .01, 3, ""},
    {"generator.segments", "Segments", "Segments", "", 3, 64, 16, 1, 0, ""},
}};
// A light's colour is its clip's Colour, dimmed by its Opacity. Intensity
// scales it; Range is how far a point or spot light reaches, fading to
// nothing there; Cone is a spot light's full angle, and Edge softness how much
// of it fades out toward its edge; Shadows is how dark what something else
// blocks it from is.
inline constexpr std::array<PropertySpec, 1> ambientLight {{
    {"light.intensity", "Intensity", "Intensity", "", 0, 4, .25, .01, 2, "×"},
}};
inline constexpr std::array<PropertySpec, 2> directionalLight {{
    {"light.intensity", "Intensity", "Intensity", "", 0, 10, 1, .01, 2, "×"},
    {"light.shadows", "Shadows", "Shadows", "", 0, 100, 100, 1, 0, "%"},
}};
inline constexpr std::array<PropertySpec, 3> pointLight {{
    {"light.intensity", "Intensity", "Intensity", "", 0, 10, 1.5, .01, 2, "×"},
    {"light.range", "Range", "Range", "", .01, 100, 6, .05, 2, ""},
    {"light.shadows", "Shadows", "Shadows", "", 0, 100, 100, 1, 0, "%"},
}};
inline constexpr std::array<PropertySpec, 5> spotLight {{
    {"light.intensity", "Intensity", "Intensity", "", 0, 10, 1.5, .01, 2, "×"},
    {"light.range", "Range", "Range", "", .01, 100, 8, .05, 2, ""},
    {"light.cone", "Cone", "Cone", "", 1, 170, 60, 1, 1, "°"},
    {"light.softness", "Edge softness", "Edge softness", "", 0, 100, 30, 1, 0, "%"},
    {"light.shadows", "Shadows", "Shadows", "", 0, 100, 100, 1, 0, "%"},
}};
}

// A light's picture in the Scene, in its own space (it shines down -Z, as a
// camera looks): the sizes its handles follow too.
namespace light_glyph {
// Ambient and point lights: a disc, and rays reaching further the brighter it is.
inline constexpr double disc = .1;
inline double rayReach(double intensity) { return .16 + .06 * intensity; }
// Directional lights: arrows from a disc, longer the brighter it is.
inline double arrowReach(double intensity) { return .2 + .08 * intensity; }
// Spot lights: a cone this long, its intensity's handle beyond it.
inline constexpr double spotLength = .3;
inline double spotReach(double intensity) { return spotLength + .06 + .06 * intensity; }
}

inline constexpr std::array<GeneratorDefinition, 23> generatorDefinitions {{
    {GeneratorKind::rectangle, "rectangle", "Rectangle", GeneratorCategory::shape, generator_specs::rectangle},
    {GeneratorKind::ellipse, "ellipse", "Ellipse", GeneratorCategory::shape, generator_specs::ellipse},
    {GeneratorKind::polygon, "polygon", "Polygon", GeneratorCategory::shape, generator_specs::polygon},
    {GeneratorKind::star, "star", "Star", GeneratorCategory::shape, generator_specs::star},
    {GeneratorKind::ring, "ring", "Ring", GeneratorCategory::shape, generator_specs::ring},
    {GeneratorKind::arc, "arc", "Arc", GeneratorCategory::curve, generator_specs::arc},
    {GeneratorKind::spiral, "spiral", "Spiral", GeneratorCategory::curve, generator_specs::spiral},
    {GeneratorKind::lissajous, "lissajous", "Lissajous", GeneratorCategory::curve, generator_specs::lissajous},
    {GeneratorKind::spirograph, "spirograph", "Spirograph", GeneratorCategory::curve, generator_specs::spirograph},
    {GeneratorKind::rose, "rose", "Rose", GeneratorCategory::curve, generator_specs::rose},
    {GeneratorKind::wave, "wave", "Wave", GeneratorCategory::curve, generator_specs::wave},
    {GeneratorKind::grid, "grid", "Grid", GeneratorCategory::grid, generator_specs::grid},
    {GeneratorKind::polarGrid, "polarGrid", "Polar grid", GeneratorCategory::grid, generator_specs::polarGrid},
    {GeneratorKind::hexGrid, "hexGrid", "Hex grid", GeneratorCategory::grid, generator_specs::hexGrid},
    {GeneratorKind::box, "box", "Box", GeneratorCategory::solid, generator_specs::box},
    {GeneratorKind::sphere, "sphere", "Sphere", GeneratorCategory::solid, generator_specs::sphere},
    {GeneratorKind::torus, "torus", "Torus", GeneratorCategory::solid, generator_specs::torus},
    {GeneratorKind::cylinder, "cylinder", "Cylinder", GeneratorCategory::solid, generator_specs::cylinder},
    {GeneratorKind::cone, "cone", "Cone", GeneratorCategory::solid, generator_specs::cone},
    {GeneratorKind::ambientLight, "ambientLight", "Ambient light", GeneratorCategory::light, generator_specs::ambientLight},
    {GeneratorKind::directionalLight, "directionalLight", "Directional light", GeneratorCategory::light, generator_specs::directionalLight},
    {GeneratorKind::pointLight, "pointLight", "Point light", GeneratorCategory::light, generator_specs::pointLight},
    {GeneratorKind::spotLight, "spotLight", "Spot light", GeneratorCategory::light, generator_specs::spotLight},
}};

inline const GeneratorDefinition* findGenerator(std::string_view id) {
    for (const auto& definition : generatorDefinitions) {
        if (definition.id == id) { return &definition; }
    }
    return nullptr;
}

inline std::string_view generatorCategoryName(GeneratorCategory category) {
    switch (category) {
        case GeneratorCategory::shape: return "Shapes";
        case GeneratorCategory::curve: return "Curves";
        case GeneratorCategory::grid: return "Grids";
        case GeneratorCategory::solid: return "3D";
        case GeneratorCategory::light: return "Lights";
    }
    return "";
}

// What one of a category is called, as a source's kind.
inline std::string_view generatorCategoryLabel(GeneratorCategory category) {
    switch (category) {
        case GeneratorCategory::shape: return "Shape";
        case GeneratorCategory::curve: return "Curve";
        case GeneratorCategory::grid: return "Grid";
        case GeneratorCategory::solid: return "3D shape";
        case GeneratorCategory::light: return "Light";
    }
    return "";
}

// A generator's asset is a ".generator" file whose text is the kind's id.
inline constexpr std::string_view generatorExtension = ".generator";
}
