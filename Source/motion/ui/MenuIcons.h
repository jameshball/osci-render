#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"
#include "InterpolationGlyph.h"

// Pictures beside menu items. Each is drawn in a 96-unit square that is the
// whole icon, so every icon in a menu scales alike, with lines fine enough to
// stay crisp once the menu shrinks it to the item's height (about 16 px).
namespace motion::ui::menu_icons {
inline constexpr float size = 96, margin = 6, line = 5;

// Shapes drawn in order over the square.
inline std::unique_ptr<juce::Drawable> inSquare(std::vector<std::unique_ptr<juce::DrawableShape>> shapes) {
    auto icon = std::make_unique<juce::DrawableComposite>();
    for (auto& shape : shapes) { icon->addChild(std::move(shape)); }
    const juce::Rectangle<float> square(0, 0, size, size);
    icon->setContentArea(square);
    icon->setBoundingBox(square);
    return icon;
}

// A path laid out in the square, stroked or filled.
inline std::unique_ptr<juce::DrawableShape> strokeOf(juce::Path path, juce::Colour colour, float thickness = line) {
    auto shape = std::make_unique<juce::DrawablePath>();
    shape->setPath(std::move(path));
    shape->setFill(juce::FillType(juce::Colours::transparentBlack));
    shape->setStrokeFill(colour);
    shape->setStrokeType(juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    return shape;
}
inline std::unique_ptr<juce::DrawableShape> fillOf(juce::Path path, juce::Colour colour) {
    auto shape = std::make_unique<juce::DrawablePath>();
    shape->setPath(std::move(path));
    shape->setFill(colour);
    return shape;
}
inline std::unique_ptr<juce::Drawable> stroked(juce::Path path, juce::Colour colour, float thickness = line) {
    std::vector<std::unique_ptr<juce::DrawableShape>> shapes;
    shapes.push_back(strokeOf(std::move(path), colour, thickness));
    return inSquare(std::move(shapes));
}
inline std::unique_ptr<juce::Drawable> filled(juce::Path path, juce::Colour colour) {
    std::vector<std::unique_ptr<juce::DrawableShape>> shapes;
    shapes.push_back(fillOf(std::move(path), colour));
    return inSquare(std::move(shapes));
}

// A path fitted into the square, keeping its proportions, at `scale` of the
// room inside the margin.
inline juce::Path fitted(juce::Path path, float scale = 1) {
    const auto room = (size - 2 * margin) * scale;
    path.applyTransform(path.getTransformToScaleToFit(juce::Rectangle<float>(room, room).withCentre({size / 2, size / 2}), true));
    return path;
}

// The colour icons take beside a menu's text.
inline juce::Colour ink() { return osci::Colours::text().withAlpha(.85f); }

// Line glyphs for adding sources, drawn like the generators' pictures.
namespace glyph {
// A pencil, its point at the lower left.
inline juce::Path pencil() {
    juce::Path path;
    path.startNewSubPath(22, -9);
    path.lineTo(84, -9);
    path.lineTo(84, 9);
    path.lineTo(22, 9);
    path.lineTo(4, 0);
    path.closeSubPath();
    path.startNewSubPath(22, -9);
    path.lineTo(22, 9);
    path.startNewSubPath(70, -9);
    path.lineTo(70, 9);
    path.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::pi / 4));
    return fitted(path, .92f);
}
// A triangle, a circle and a square: things made from a few numbers.
inline juce::Path shapes() {
    juce::Path path;
    path.addTriangle(48, 8, 70, 44, 26, 44);
    path.addEllipse(10, 54, 32, 32);
    path.addRectangle(56, 54, 30, 30);
    return fitted(path);
}
// A folder, its tab at the upper left.
inline juce::Path folder() {
    juce::Path path;
    path.startNewSubPath(8, 22);
    path.lineTo(36, 22);
    path.lineTo(44, 30);
    path.lineTo(88, 30);
    path.lineTo(88, 78);
    path.lineTo(8, 78);
    path.closeSubPath();
    path.startNewSubPath(8, 38);
    path.lineTo(88, 38);
    return fitted(path);
}
// A point sending in both directions: a source that arrives live.
inline juce::Path live() {
    juce::Path path;
    path.addEllipse(41, 41, 14, 14);
    constexpr auto pi = juce::MathConstants<float>::pi;
    for (const auto radius : {22.0f, 38.0f}) {
        path.addCentredArc(48, 48, radius, radius, 0, pi / 4, pi * 3 / 4, true);
        path.addCentredArc(48, 48, radius, radius, 0, -pi * 3 / 4, -pi / 4, true);
    }
    return fitted(path);
}
// A wire cube: a 3D model.
inline juce::Path cube() {
    juce::Path path;
    path.startNewSubPath(48, 8);
    path.lineTo(84, 26);
    path.lineTo(84, 68);
    path.lineTo(48, 88);
    path.lineTo(12, 68);
    path.lineTo(12, 26);
    path.closeSubPath();
    path.startNewSubPath(12, 26);
    path.lineTo(48, 46);
    path.lineTo(84, 26);
    path.startNewSubPath(48, 46);
    path.lineTo(48, 88);
    return fitted(path);
}
// Braces: a script.
inline juce::Path braces() {
    juce::Path path;
    for (const auto side : {-1.0f, 1.0f}) {
        const auto x = 48 + side * 30, inner = 48 + side * 20;
        path.startNewSubPath(inner, 10);
        path.quadraticTo(x, 10, x, 22);
        path.lineTo(x, 38);
        path.quadraticTo(x, 48, x + side * 8, 48);
        path.quadraticTo(x, 48, x, 58);
        path.lineTo(x, 74);
        path.quadraticTo(x, 86, inner, 86);
    }
    return fitted(path, .9f);
}
// A play button: an animation.
inline juce::Path play() {
    juce::Path path;
    path.addRoundedRectangle(8, 14, 80, 68, 14);
    path.addTriangle(40, 32, 40, 64, 64, 48);
    return fitted(path);
}
// A branching tree: a fractal.
inline juce::Path tree() {
    juce::Path path;
    const auto branch = [&path](auto&& self, juce::Point<float> from, float angle, float length, int depth) -> void {
        const auto to = from + juce::Point<float>(std::sin(angle), -std::cos(angle)) * length;
        path.startNewSubPath(from);
        path.lineTo(to);
        if (depth == 0) { return; }
        self(self, to, angle - .6f, length * .64f, depth - 1);
        self(self, to, angle + .6f, length * .64f, depth - 1);
    };
    branch(branch, {48, 90}, 0, 30, 3);
    return fitted(path);
}
// A letter T: text.
inline juce::Path letter() {
    juce::Path path;
    path.startNewSubPath(14, 22);
    path.lineTo(14, 12);
    path.lineTo(82, 12);
    path.lineTo(82, 22);
    path.startNewSubPath(48, 12);
    path.lineTo(48, 86);
    path.startNewSubPath(36, 86);
    path.lineTo(60, 86);
    return fitted(path, .9f);
}
// An open book.
inline juce::Path book() {
    juce::Path path;
    path.startNewSubPath(48, 28);
    path.quadraticTo(30, 18, 8, 22);
    path.lineTo(8, 76);
    path.quadraticTo(30, 72, 48, 82);
    path.quadraticTo(66, 72, 88, 76);
    path.lineTo(88, 22);
    path.quadraticTo(66, 18, 48, 28);
    path.closeSubPath();
    path.startNewSubPath(48, 28);
    path.lineTo(48, 82);
    return fitted(path);
}
}

// An empty icon, which keeps an item's text in line with its neighbours'
// while its picture is still being made.
inline std::unique_ptr<juce::Drawable> blank() { return inSquare({}); }

// A picture of a source traced into a unit square (y down), drawn once into
// an image `pixels` across, white on clear: the more line there is, the finer
// it is drawn, so a busy model keeps its outline rather than filling in. A
// traced model can be thousands of points, too many to stroke every time a
// menu opens or repaints.
inline juce::Image pictureMask(juce::Path unit, int pixels) {
    unit.applyTransform(juce::AffineTransform::scale(size - 2 * margin).translated(margin, margin));
    float length = 0;
    for (juce::PathFlatteningIterator segment(unit); segment.next();) { length += juce::Point<float>(segment.x1, segment.y1).getDistanceFrom({segment.x2, segment.y2}); }
    // About eight sides of the square's worth of line is drawn at full weight.
    const auto thickness = juce::jlimit(1.2f, line, line * std::sqrt(8 * size / std::max(length, 1.0f)));
    juce::Path stroke;
    juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(stroke, unit);
    juce::Image image(juce::Image::ARGB, pixels, pixels, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.setColour(juce::Colours::white);
    g.fillPath(stroke, juce::AffineTransform::scale(static_cast<float>(pixels) / size));
    return image;
}

// The picture as a menu item's icon, in the colour icons take.
inline std::unique_ptr<juce::Drawable> picture(const juce::Image& mask) {
    auto icon = std::make_unique<juce::DrawableImage>(mask);
    icon->setOverlayColour(ink());
    icon->setBoundingBox(juce::Rectangle<float>(0, 0, size, size));
    return icon;
}

// A key menu's interpolations, and its eases, as the Key panel draws them.
inline std::unique_ptr<juce::Drawable> segment(const SegmentGlyph& glyph) {
    std::vector<std::unique_ptr<juce::DrawableShape>> shapes;
    shapes.push_back(strokeOf(glyph.segment, ink(), glyph.stroke));
    if (!glyph.handles.isEmpty()) {
        auto handles = std::make_unique<juce::DrawablePath>();
        handles->setPath(glyph.handles);
        handles->setFill(juce::FillType());
        handles->setStrokeFill(ink().withMultipliedAlpha(.55f));
        handles->setStrokeType(juce::PathStrokeType(glyph.handleStroke, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        shapes.push_back(std::move(handles));
    }
    juce::Path keys;
    for (const auto end : glyph.keys) { keys.addEllipse(juce::Rectangle<float>(glyph.keySize, glyph.keySize).withCentre(end)); }
    shapes.push_back(fillOf(keys, ink()));
    return inSquare(std::move(shapes));
}
// The Key panel's box (14 by 12), scaled to fill an icon's square.
inline juce::Rectangle<float> segmentBox() { return juce::Rectangle<float>(84, 72).withCentre({size / 2, size / 2}); }
inline std::unique_ptr<juce::Drawable> interpolation(motion::Interpolation kind) { return segment(interpolationGlyph(segmentBox(), kind)); }
inline std::unique_ptr<juce::Drawable> ease(bool leaving, bool arriving) { return segment(easeGlyph(segmentBox(), leaving, arriving)); }

// A label colour: a rounded swatch, or an empty outline for none.
inline std::unique_ptr<juce::Drawable> swatch(std::optional<juce::Colour> colour) {
    const auto box = juce::Rectangle<float>(54, 54).withCentre({size / 2, size / 2});
    juce::Path path;
    path.addRoundedRectangle(box, 12);
    if (colour.has_value()) { return filled(path, *colour); }
    return stroked(path, ink().withMultipliedAlpha(.7f));
}

// Which side of a cube a view looks at: the cube turned to show three
// sides, its edges drawn and the side looked at filled (a side out of sight
// shows through, fainter).
enum class Side { front, right, top, back, left, bottom };
inline std::unique_ptr<juce::Drawable> viewCube(Side side) {
    constexpr float turn = -0.6f, tilt = 0.45f;
    const auto at = [](float x, float y, float z) {
        const auto rx = x * std::cos(turn) + z * std::sin(turn);
        const auto rz = -x * std::sin(turn) + z * std::cos(turn);
        return juce::Point<float>(rx, -(y * std::cos(tilt) - rz * std::sin(tilt)));
    };
    // Corners by their signs, (x, y, z) each -1 or 1.
    const auto corner = [&](int index) { return at(index & 1 ? 1.f : -1.f, index & 2 ? 1.f : -1.f, index & 4 ? 1.f : -1.f); };
    juce::Path edges;
    for (int a = 0; a < 8; ++a) {
        for (const auto bit : {1, 2, 4}) {
            if ((a & bit) != 0) { continue; }
            edges.startNewSubPath(corner(a));
            edges.lineTo(corner(a | bit));
        }
    }
    // The side's corners, in order round it.
    const auto face = [&](int axis, bool positive) {
        const std::array<int, 3> bits {1, 2, 4};
        const auto fixed = positive ? bits[static_cast<std::size_t>(axis)] : 0;
        const auto u = bits[static_cast<std::size_t>((axis + 1) % 3)], v = bits[static_cast<std::size_t>((axis + 2) % 3)];
        juce::Path path;
        path.startNewSubPath(corner(fixed));
        path.lineTo(corner(fixed | u));
        path.lineTo(corner(fixed | u | v));
        path.lineTo(corner(fixed | v));
        path.closeSubPath();
        return path;
    };
    const auto axis = side == Side::front || side == Side::back ? 2 : side == Side::right || side == Side::left ? 0 : 1;
    const auto positive = side == Side::front || side == Side::right || side == Side::top;
    auto shaded = face(axis, positive);
    // Both laid out together, so the face sits where its edges are.
    const auto layout = edges.getTransformToScaleToFit(juce::Rectangle<float>(size - 2 * margin, size - 2 * margin).withCentre({size / 2, size / 2}), true);
    edges.applyTransform(layout);
    shaded.applyTransform(layout);
    std::vector<std::unique_ptr<juce::DrawableShape>> shapes;
    shapes.push_back(fillOf(shaded, style::key().withAlpha(positive ? .75f : .4f)));
    shapes.push_back(strokeOf(edges, ink()));
    return inSquare(std::move(shapes));
}
}
