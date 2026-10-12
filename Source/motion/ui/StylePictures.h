#pragma once

#include <JuceHeader.h>
#include "../model/LightPresets.h"
#include "../render/Reflections.h"
#include "../render/HiddenLineHatching.h"
#include "../render/SierpinskiHatching.h"
#include "../render/SurfaceLines.h"

// Pictures of how a surface can draw itself, and of a light's presets, for
// tiles to choose them by. A surface's are a ball drawn that way by the same
// geometry the Scope's lines come from, seen from a little above and to the
// side. Each is in a square from -1 to 1 (y down), so they share one scale.
namespace motion::ui::pictures {
// What a surface tile shows: its lines, and faintly the ball they are on.
struct Picture {
    juce::Path lines, context;
};

enum class SurfaceLook { edges, outline, spiral, hilbert, noise, slices, rings, mirror };

namespace detail {
struct Ball {
    std::vector<Vec3> corners;
    std::vector<hidden::surface::Face> faces;
    hidden::surface::Normals normals;
    int segments = 24, rings = 12;
};
// A ball of radius 1, already turned to be seen from +z.
inline const Ball& ball() {
    static const Ball made = [] {
        Ball result;
        const auto pi = juce::MathConstants<double>::pi;
        for (int ring = 0; ring <= result.rings; ++ring) {
            for (int segment = 0; segment < result.segments; ++segment) {
                const auto latitude = pi * ring / result.rings - pi / 2, longitude = 2 * pi * segment / result.segments;
                result.corners.push_back(Vec3 {std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)});
            }
        }
        for (std::uint32_t ring = 0; ring < static_cast<std::uint32_t>(result.rings); ++ring) {
            for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(result.segments); ++segment) {
                const auto segments = static_cast<std::uint32_t>(result.segments);
                const auto a = ring * segments + segment, b = ring * segments + (segment + 1) % segments;
                result.faces.push_back({a, b + segments, b});
                result.faces.push_back({a, a + segments, b + segments});
            }
        }
        result.normals = hidden::surface::normalsOf(result.corners, result.faces);
        return result;
    }();
    return made;
}
// The view: turned a little, tipped to look down on it.
inline Vec3 turned(Vec3 point) {
    constexpr double yaw = -.45, tip = .38;
    const auto x = point.x * std::cos(yaw) + point.z * std::sin(yaw);
    const auto z = -point.x * std::sin(yaw) + point.z * std::cos(yaw);
    return {x, point.y * std::cos(tip) - z * std::sin(tip), point.y * std::sin(tip) + z * std::cos(tip)};
}
inline juce::Point<float> onTile(Vec3 point) {
    const auto seen = turned(point);
    return {static_cast<float>(seen.x), static_cast<float>(-seen.y)};
}
// Whether a point of the ball (its own normal) faces the eye.
inline bool facing(Vec3 point) { return turned(point).z > 0; }

// A line over the ball, `at(share)` along it, where it faces the eye: in
// `steps`, and to exactly where it turns away, so it meets the rim.
template <typename At>
void traceFacing(juce::Path& path, At&& at, int steps) {
    const auto rim = [&](double near, double far) {
        for (int halving = 0; halving < 30; ++halving) {
            const auto middle = (near + far) * .5;
            (facing(at(middle)) ? near : far) = middle;
        }
        return at(near);
    };
    bool open = false;
    double previous = 0;
    for (int step = 0; step <= steps; ++step) {
        const auto share = static_cast<double>(step) / steps;
        const auto point = at(share);
        if (!facing(point)) {
            if (open) { path.lineTo(onTile(rim(previous, share))); }
            open = false;
        } else if (open) {
            path.lineTo(onTile(point));
        } else {
            path.startNewSubPath(onTile(step == 0 ? point : rim(share, previous)));
            if (step > 0) { path.lineTo(onTile(point)); }
            open = true;
        }
        previous = share;
    }
}

// Pieces laid on the ball, where they face the eye, following its curve.
inline juce::Path laid(const std::vector<hidden::surface::Piece>& pieces) {
    const auto& made = ball();
    juce::Path path;
    for (const auto& piece : pieces) {
        traceFacing(path, [&](double share) {
            hidden::surface::Weights weights;
            for (std::size_t corner = 0; corner < 3; ++corner) { weights[corner] = piece.from[corner] + (piece.to[corner] - piece.from[corner]) * share; }
            return hidden::surface::smoothAt(made.corners, made.faces, made.normals, piece.face, weights);
        }, 3);
    }
    return path;
}

inline juce::Path circle() {
    juce::Path path;
    path.addEllipse(-1, -1, 2, 2);
    return path;
}

// The ball's parallels and meridians, its near side.
inline juce::Path edges() {
    const auto pi = juce::MathConstants<double>::pi;
    juce::Path path;
    const auto trace = [&](auto&& at) { traceFacing(path, at, 96); };
    constexpr int rings = 6, segments = 10;
    for (int ring = 1; ring < rings; ++ring) {
        const auto latitude = pi * ring / rings - pi / 2;
        trace([&](double share) { return Vec3 {std::cos(latitude) * std::cos(2 * pi * share), std::sin(latitude), std::cos(latitude) * std::sin(2 * pi * share)}; });
    }
    for (int segment = 0; segment < segments; ++segment) {
        const auto longitude = 2 * pi * segment / segments;
        trace([&](double share) {
            const auto latitude = pi * share - pi / 2;
            return Vec3 {std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)};
        });
    }
    return path;
}

// A floor of squares below the ball, as the ball shows it from far in front.
inline juce::Path reflection() {
    const hidden::SphereMirror mirror {{0, 0, 0}, 1};
    // The eye, far off along the view, in the ball's own space.
    const auto back = [](Vec3 seen) {
        constexpr double yaw = -.45, tip = .38;
        const auto y = seen.y * std::cos(tip) + seen.z * std::sin(tip);
        const auto z = -seen.y * std::sin(tip) + seen.z * std::cos(tip);
        return Vec3 {seen.x * std::cos(yaw) - z * std::sin(yaw), y, seen.x * std::sin(yaw) + z * std::cos(yaw)};
    };
    const auto eye = back({0, 0, 60});
    juce::Path path;
    const auto trace = [&](Vec3 from, Vec3 to) {
        constexpr int steps = 160;
        bool open = false;
        for (int step = 0; step <= steps; ++step) {
            const auto seen = mirror.reflectionOf(eye, from + (to - from) * (static_cast<double>(step) / steps));
            if (!seen.has_value() || !facing(*seen)) {
                open = false;
                continue;
            }
            if (open) { path.lineTo(onTile(*seen)); } else { path.startNewSubPath(onTile(*seen)); }
            open = true;
        }
    };
    constexpr double floor = -1.4, reach = 8, spacing = 1.3;
    for (int line = -2; line <= 2; ++line) {
        trace({line * spacing, floor, -reach}, {line * spacing, floor, reach});
        trace({-reach, floor, line * spacing}, {reach, floor, line * spacing});
    }
    return path;
}
}

inline const Picture& surfacePicture(SurfaceLook look) {
    static const auto made = [] {
        using hidden::surface::Pattern;
        const auto& ball = detail::ball();
        const auto pattern = [&](Pattern kind, double across) { return detail::laid(hidden::surface::pattern(ball.corners, ball.faces, kind, across, .15)); };
        std::array<Picture, 8> result;
        const auto outline = detail::circle();
        auto edges = detail::edges();
        edges.addPath(outline);
        result[static_cast<std::size_t>(SurfaceLook::edges)] = {edges, {}};
        result[static_cast<std::size_t>(SurfaceLook::outline)] = {outline, {}};
        result[static_cast<std::size_t>(SurfaceLook::spiral)] = {pattern(Pattern::spiral, 5), outline};
        result[static_cast<std::size_t>(SurfaceLook::hilbert)] = {pattern(Pattern::hilbert, 8), outline};
        result[static_cast<std::size_t>(SurfaceLook::noise)] = {pattern(Pattern::noise, 6), outline};
        result[static_cast<std::size_t>(SurfaceLook::slices)] = {pattern(Pattern::slices, 7), outline};
        result[static_cast<std::size_t>(SurfaceLook::rings)] = {pattern(Pattern::rings, 6), outline};
        auto mirror = outline;
        mirror.addPath(detail::reflection());
        result[static_cast<std::size_t>(SurfaceLook::mirror)] = {mirror, {}};
        return result;
    }();
    return made[static_cast<std::size_t>(look)];
}

// How a camera shades what light falls on: by brightness (a ball's lines
// bright where it is lit and faint where not), or by hatching (a Hilbert
// curve over it, dense where it is lit).
enum class Shading { brightness, hatching, frameHatching };
inline const Picture& shadingPicture(Shading shading) {
    static const auto made = [] {
        std::array<Picture, 3> result;
        const Vec3 toward = Vec3 {-.6, .55, .58}.normalized().value();
        // The ball's lines, split by how lit they are.
        Picture bright;
        bright.lines.addEllipse(-1, -1, 2, 2);
        const auto pi = juce::MathConstants<double>::pi;
        // Each short stretch bright or faint as the light falls on its middle.
        const auto stretch = [&](Vec3 from, Vec3 to) {
            if (!detail::facing(from) || !detail::facing(to)) { return; }
            auto& path = ((from + to) * .5).dot(toward) > .15 ? bright.lines : bright.context;
            path.startNewSubPath(detail::onTile(from));
            path.lineTo(detail::onTile(to));
        };
        const auto on = [&](double latitude, double longitude) { return Vec3 {std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)}; };
        for (int ring = 1; ring < 6; ++ring) {
            const auto latitude = pi * ring / 6 - pi / 2;
            for (int step = 0; step < 64; ++step) { stretch(on(latitude, 2 * pi * step / 64), on(latitude, 2 * pi * (step + 1) / 64)); }
        }
        for (int meridian = 0; meridian < 10; ++meridian) {
            const auto longitude = 2 * pi * meridian / 10;
            for (int step = 0; step < 32; ++step) { stretch(on(pi * step / 32 - pi / 2, longitude), on(pi * (step + 1) / 32 - pi / 2, longitude)); }
        }
        result[static_cast<std::size_t>(Shading::brightness)] = bright;
        // A disc lit from the upper left, hatched by the curve itself.
        Picture hatched;
        hatched.context.addEllipse(-1, -1, 2, 2);
        const auto look = [&](double x, double y) -> std::optional<hidden::hatching::Seen> {
            const auto r = x * x + y * y;
            if (r > .97) { return std::nullopt; }
            // On the tile y runs down.
            const Vec3 normal {x, y, std::sqrt(1 - r)};
            return hidden::hatching::Seen {0, {x, -y, 0}, std::max(0.0, normal.dot(toward))};
        };
        bool open = false;
        for (const auto& stroke : hidden::hatching::strokes(1, 32, look)) {
            open = false;
            for (const auto& point : stroke.points) {
                const juce::Point<float> at(static_cast<float>(point.at.x), static_cast<float>(point.at.y));
                if (open) { hatched.lines.lineTo(at); } else { hatched.lines.startNewSubPath(at); }
                open = true;
            }
        }
        // Over the frame: the disc hatched as the frame's grid falls on it,
        // in its frame.
        hatched.context.addRectangle(-1.0f, -1.0f, 2.0f, 2.0f);
        result[static_cast<std::size_t>(Shading::frameHatching)] = hatched;
        // On the ball itself, one curve over its faces, lit from the upper left.
        const auto& made = detail::ball();
        const auto curve = hidden::hatching::sierpinski::Curve::of(made.corners, made.faces, .14);
        const auto middleOf = [&](std::uint32_t node) {
            const auto [face, weights] = curve.middleOf(node);
            return std::pair {hidden::surface::smoothAt(made.corners, made.faces, made.normals, face, weights), made.normals.at(face, weights)};
        };
        const auto lit = hidden::hatching::sierpinski::Lit::of(curve, [&](std::uint32_t leaf) -> double {
            const auto [at, normal] = middleOf(leaf);
            if (!detail::facing(normal)) { return -1; }
            return std::max(0.0, normal.dot(toward)) * std::abs(detail::turned(normal).z);
        }, [&](std::uint32_t leaf) {
            const auto& node = curve.nodes[leaf];
            const auto& face = made.faces[node.face];
            const auto a = hidden::surface::at(made.corners, face, node.corners[0]), b = hidden::surface::at(made.corners, face, node.corners[1]), c = hidden::surface::at(made.corners, face, node.corners[2]);
            return (b - a).cross(c - a).length() / 2;
        }, [&](std::uint32_t leaf) { return middleOf(leaf).first; });
        juce::Path sierpinskiLines;
        bool drawing = false;
        lit.curveAt(.14, lit.brightest(), 1, [&](std::optional<std::uint32_t> node, double light) {
            if (!node.has_value() || light < hidden::hatching::darkest) {
                drawing = false;
                return;
            }
            const auto at = detail::onTile(middleOf(*node).first);
            if (drawing) { sierpinskiLines.lineTo(at); } else { sierpinskiLines.startNewSubPath(at); }
            drawing = true;
        });
        Picture onBall;
        onBall.lines = sierpinskiLines;
        onBall.context.addEllipse(-1, -1, 2, 2);
        result[static_cast<std::size_t>(Shading::hatching)] = onBall;
        return result;
    }();
    return made[static_cast<std::size_t>(shading)];
}

// A preset's name short enough for a small tile.
inline juce::String lightPresetShortName(LightPreset preset) {
    return preset == LightPreset::flickeringBulb ? juce::String("Flicker") : juce::String(lightPresetName(preset).data());
}

// A light's preset, or none: a glyph of how it moves on its own.
inline juce::Path lightPresetPicture(std::optional<LightPreset> preset) {
    juce::Path path;
    if (!preset.has_value()) {
        // A steady light: a small sun.
        path.addEllipse(-.32f, -.32f, .64f, .64f);
        for (int ray = 0; ray < 8; ++ray) {
            const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(ray) / 8;
            path.startNewSubPath(std::cos(angle) * .52f, std::sin(angle) * .52f);
            path.lineTo(std::cos(angle) * .78f, std::sin(angle) * .78f);
        }
        return path;
    }
    switch (*preset) {
        case LightPreset::fire:
            // Two tongues of flame.
            path.startNewSubPath(0, .85f);
            path.cubicTo(-.62f, .85f, -.72f, .25f, -.38f, -.12f);
            path.cubicTo(-.3f, .12f, -.18f, .2f, -.08f, .16f);
            path.cubicTo(-.28f, -.3f, .02f, -.62f, .12f, -.88f);
            path.cubicTo(.3f, -.5f, .72f, -.2f, .62f, .32f);
            path.cubicTo(.56f, .68f, .3f, .85f, 0, .85f);
            path.closeSubPath();
            path.startNewSubPath(0, .85f);
            path.cubicTo(-.24f, .85f, -.3f, .55f, -.12f, .3f);
            path.cubicTo(-.02f, .42f, .08f, .4f, .1f, .26f);
            path.cubicTo(.26f, .44f, .3f, .82f, 0, .85f);
            break;
        case LightPreset::candle:
            path.addRoundedRectangle(-.24f, -.02f, .48f, .9f, .05f);
            path.startNewSubPath(0, -.12f);
            path.cubicTo(-.2f, -.22f, -.16f, -.5f, 0, -.82f);
            path.cubicTo(.16f, -.5f, .2f, -.22f, 0, -.12f);
            path.closeSubPath();
            break;
        case LightPreset::flickeringBulb:
            path.startNewSubPath(-.2f, .42f);
            path.cubicTo(-.2f, .22f, -.48f, .1f, -.48f, -.22f);
            path.cubicTo(-.48f, -.56f, -.24f, -.76f, 0, -.76f);
            path.cubicTo(.24f, -.76f, .48f, -.56f, .48f, -.22f);
            path.cubicTo(.48f, .1f, .2f, .22f, .2f, .42f);
            path.closeSubPath();
            path.startNewSubPath(-.16f, .58f);
            path.lineTo(.16f, .58f);
            path.startNewSubPath(-.1f, .74f);
            path.lineTo(.1f, .74f);
            // Its flicker: a broken zigzag either side.
            for (const auto side : {-1.0f, 1.0f}) {
                path.startNewSubPath(side * .62f, -.6f);
                path.lineTo(side * .78f, -.38f);
                path.lineTo(side * .64f, -.24f);
                path.lineTo(side * .82f, 0);
            }
            break;
        case LightPreset::pulse:
            path.startNewSubPath(-.9f, .1f);
            for (int step = 1; step <= 48; ++step) {
                const auto x = -.9f + 1.8f * static_cast<float>(step) / 48;
                path.lineTo(x, .1f - .62f * std::pow(std::sin((x + .9f) / 1.8f * juce::MathConstants<float>::pi * 2), 2.0f));
            }
            break;
        case LightPreset::strobe:
            path.startNewSubPath(-.9f, .5f);
            for (const auto x : {-.6f, -.3f, 0.0f, .3f, .6f}) {
                path.lineTo(x, .5f);
                path.lineTo(x, -.5f);
                path.lineTo(x + .1f, -.5f);
                path.lineTo(x + .1f, .5f);
            }
            path.lineTo(.9f, .5f);
            break;
    }
    return path;
}
}
