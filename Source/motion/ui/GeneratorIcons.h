#pragma once

#include <JuceHeader.h>
#include "../model/GeneratorGeometry.h"
#include "MenuIcons.h"

namespace motion::ui {
// A generator's picture at its defaults: its strokes, a solid turned to show
// three of its sides, fitted into a square of `size` with `margin` to spare.
inline juce::Path generatorPicture(const GeneratorDefinition& definition, float size, float margin) {
    std::vector<double> defaults;
    for (const auto& spec : definition.parameters) { defaults.push_back(spec.defaultValue); }
    // Fewer cells and segments read better this small.
    switch (definition.kind) {
        case GeneratorKind::grid: defaults = {4, 4, 1, 1}; break;
        case GeneratorKind::polarGrid: defaults = {3, 8, 1}; break;
        case GeneratorKind::hexGrid: defaults = {3, 3, .2}; break;
        case GeneratorKind::sphere: defaults = {.5, 8, 5}; break;
        case GeneratorKind::torus: defaults = {.4, .16, 12, 6}; break;
        case GeneratorKind::cylinder: case GeneratorKind::cone: defaults[2] = 8; break;
        default: break;
    }
    const auto geometry = generator::build(definition, defaults);
    // Solids and lights (which shine one way, in depth) are seen in three dimensions.
    const auto solid = definition.category == GeneratorCategory::solid || definition.category == GeneratorCategory::light;
    // Seen from a little above and to the side (a torus, lying flat, from
    // higher up); y up becomes y down.
    const auto tilt = definition.kind == GeneratorKind::torus ? 0.95 : 0.45;
    const auto view = [solid, tilt](const Vec3& point) {
        if (!solid) { return juce::Point<float>(static_cast<float>(point.x), static_cast<float>(-point.y)); }
        constexpr double turn = -0.6;
        const auto x = point.x * std::cos(turn) + point.z * std::sin(turn);
        const auto z = -point.x * std::sin(turn) + point.z * std::cos(turn);
        const auto y = point.y * std::cos(tilt) - z * std::sin(tilt);
        return juce::Point<float>(static_cast<float>(x), static_cast<float>(-y));
    };
    juce::Path path;
    for (const auto& stroke : geometry.paths) {
        for (std::size_t index = 0; index < stroke.size(); ++index) {
            const auto& points = stroke[index].points;
            if (index == 0) { path.startNewSubPath(view(points[0])); }
            if (stroke[index].curve && !solid) {
                path.cubicTo(view(points[1]), view(points[2]), view(points[3]));
            } else {
                path.lineTo(view(points[3]));
            }
        }
    }
    path.applyTransform(path.getTransformToScaleToFit(juce::Rectangle<float>(margin, margin, size - 2 * margin, size - 2 * margin), true));
    return path;
}

// The picture as a menu item's icon, stroked in `colour`.
inline std::unique_ptr<juce::Drawable> generatorIcon(const GeneratorDefinition& definition, juce::Colour colour) {
    return menu_icons::stroked(generatorPicture(definition, menu_icons::size, menu_icons::margin), colour);
}
}
