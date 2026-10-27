#pragma once

#include <JuceHeader.h>
#include "../model/GeneratorGeometry.h"
#include "MenuIcons.h"

namespace motion::ui {
// A light's glyph, in a square from -1 to 1 (y down): drawn plainly rather
// than from its gizmo, which is too fine to read this small.
inline juce::Path lightGlyph(GeneratorKind kind) {
    juce::Path path;
    const auto pi = juce::MathConstants<float>::pi;
    const auto rays = [&](int count, float from, float to) {
        for (int ray = 0; ray < count; ++ray) {
            const auto angle = 2 * pi * static_cast<float>(ray) / static_cast<float>(count);
            path.startNewSubPath(std::cos(angle) * from, std::sin(angle) * from);
            path.lineTo(std::cos(angle) * to, std::sin(angle) * to);
        }
    };
    switch (kind) {
        case GeneratorKind::spotLight:
            // A lamp and the cone it throws down.
            path.addEllipse(-.18f, -.86f, .36f, .36f);
            path.startNewSubPath(-.14f, -.46f);
            path.lineTo(-.66f, .5f);
            path.startNewSubPath(.14f, -.46f);
            path.lineTo(.66f, .5f);
            path.startNewSubPath(-.66f, .5f);
            path.quadraticTo(0, .9f, .66f, .5f);
            break;
        case GeneratorKind::directionalLight:
            // Parallel rays, all one way.
            for (const auto offset : {-.5f, 0.0f, .5f}) {
                const juce::Point<float> start(-.52f + offset * .7f, -.52f - offset * .7f), end(.52f + offset * .7f, .52f - offset * .7f);
                path.startNewSubPath(start);
                path.lineTo(end);
                path.startNewSubPath(end.x - .3f, end.y);
                path.lineTo(end);
                path.lineTo(end.x, end.y - .3f);
            }
            break;
        case GeneratorKind::ambientLight:
            // Light from all round: a soft glow, its edge broken.
            path.addEllipse(-.22f, -.22f, .44f, .44f);
            for (int dash = 0; dash < 12; ++dash) {
                const auto from = 2 * pi * (static_cast<float>(dash) + .2f) / 12, to = 2 * pi * (static_cast<float>(dash) + .8f) / 12;
                path.addCentredArc(0, 0, .8f, .8f, 0, from, to, true);
            }
            break;
        case GeneratorKind::pointLight:
        default:
            // A point shining every way.
            path.addEllipse(-.26f, -.26f, .52f, .52f);
            rays(8, .46f, .82f);
            break;
    }
    return path;
}

// A generator's picture at its defaults: its strokes, a solid turned to show
// three of its sides, fitted into a square of `size` with `margin` to spare.
inline juce::Path generatorPicture(const GeneratorDefinition& definition, float size, float margin) {
    if (definition.category == GeneratorCategory::light) {
        auto path = lightGlyph(definition.kind);
        path.applyTransform(juce::AffineTransform::scale((size - 2 * margin) / 2).translated(size / 2, size / 2));
        return path;
    }
    std::vector<double> defaults;
    for (const auto& spec : definition.parameters) { defaults.push_back(spec.defaultValue); }
    // Fewer cells and segments read better this small.
    switch (definition.kind) {
        case GeneratorKind::grid: defaults = {4, 4, 1, 1}; break;
        case GeneratorKind::polarGrid: defaults = {3, 8, 1}; break;
        case GeneratorKind::hexGrid: defaults = {3, 3, .2}; break;
        case GeneratorKind::sphere: defaults = {.5, 6, 4}; break;
        case GeneratorKind::torus: defaults = {.4, .16, 10, 4}; break;
        case GeneratorKind::cylinder: case GeneratorKind::cone: defaults[2] = 6; break;
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
