#pragma once

#include "GeneratorGeometry.h"
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace motion::generator {
// Handles on a generator's drawing, so its numbers are changed on the shape
// itself, as in Figma or Illustrator: a star's tip and inner corner set its
// radii, a rectangle's corner its size, an arc's ends its angles, a box's
// faces its width, height and depth. A count (a star's points, a grid's
// rows) is stepped on a handle, by scrolling, or by a count handle's own
// sideways drag. Everything here is in the generator's own space, before its
// clip's transform, flat drawings lying in z = 0.
struct Handle {
    enum class Kind { point, count };
    Kind kind = Kind::point;
    Vec3 at;
    // A point dragged along this own-space axis; none for one dragged
    // across the drawing's plane.
    std::optional<Vec3> axis;
    // The parameters a drag sets, and the count it steps, by index.
    std::vector<std::size_t> parameters;
    std::optional<std::size_t> count;
    // What it changes, as a hint names it.
    std::string label;
};

namespace handle_detail {
constexpr double pi = std::numbers::pi;
inline Vec3 polar(double radius, double degrees) { return {radius * std::cos(degrees * pi / 180), radius * std::sin(degrees * pi / 180), 0}; }
inline double flatLength(Vec3 value) { return std::hypot(value.x, value.y); }
inline double degreesOf(Vec3 value) { return std::atan2(value.y, value.x) * 180 / pi; }
// The turn from one angle to another, the short way round.
inline double turn(double from, double to) {
    auto delta = std::fmod(to - from, 360.0);
    if (delta > 180) { delta -= 360; }
    if (delta <= -180) { delta += 360; }
    return delta;
}
inline std::string label(const GeneratorDefinition& definition, std::size_t parameter) {
    const auto& spec = definition.parameters[parameter];
    return std::string(spec.label);
}
inline Handle point(Vec3 at, std::vector<std::size_t> parameters, std::string label, std::optional<std::size_t> count = std::nullopt, std::optional<Vec3> axis = std::nullopt) {
    return {Handle::Kind::point, at, axis, std::move(parameters), count, std::move(label)};
}
inline Handle counter(Vec3 at, std::size_t count, std::string label) {
    return {Handle::Kind::count, at, std::nullopt, {}, count, std::move(label)};
}
// A corner's roundness handle, along the edge toward `next`: a little way in
// even when the corner is sharp, so it is never on the corner's own handle,
// and as far as the cut reaches when it is round (all the way at 100%).
constexpr double roundnessInset = .4;
inline Vec3 roundnessPoint(Vec3 corner, Vec3 previous, Vec3 next, double roundness) {
    const auto half = .5 * std::min((previous - corner).length(), (next - corner).length());
    const auto toward = (next - corner).normalized(1.0e-12).value_or(Vec3 {});
    return corner + toward * (half * (roundnessInset + (1 - roundnessInset) * roundness / 100));
}
// The roundness that puts that handle at a point dragged along the edge.
inline double roundnessAt(Vec3 corner, Vec3 previous, Vec3 next, Vec3 to) {
    const auto half = .5 * std::min((previous - corner).length(), (next - corner).length());
    const auto toward = (next - corner).normalized(1.0e-12);
    if (!toward.has_value() || !(half > 0)) { return 0; }
    const auto reach = (to - corner).dot(*toward) / half;
    return std::clamp((reach - roundnessInset) / (1 - roundnessInset), 0.0, 1.0) * 100;
}
inline std::vector<Vec3> starCorners(int points, double outer, double inner) {
    std::vector<Vec3> corners;
    for (int index = 0; index < 2 * points; ++index) { corners.push_back(polar(index % 2 == 0 ? outer : inner, 90 + 180.0 * index / points)); }
    return corners;
}
// Spirograph: a gear of `teeth` inside a ring of 96, its pen `pen` of the
// way out. The pen's handle sits on the left as far in as the loops reach,
// so it follows them (and clears the transform gizmo's arrows).
constexpr double ringTeeth = 96;
inline double spirographInner(double teeth, double pen) {
    const auto rolling = ringTeeth - teeth, d = pen / 100 * teeth;
    return rolling + d > 0 ? (rolling - d) / (rolling + d) : 0;
}
// The first crest of a wave, as its share of the length (none when flat).
inline std::optional<double> waveCrest(double cycles, double phaseDegrees) {
    if (!(cycles > 0)) { return std::nullopt; }
    auto angle = std::fmod(pi / 2 - phaseDegrees * pi / 180, 2 * pi);
    if (angle < 0) { angle += 2 * pi; }
    return angle / (2 * pi * cycles);
}
struct Hex {
    double across, down, width, height;
    Vec3 centre(int row, int column, double size) const {
        return {-width / 2 + across / 2 + across * column + (row % 2 == 1 ? across / 2 : 0), height / 2 - size - down * row, 0};
    }
};
inline Hex hex(int columns, int rows, double size) {
    const auto across = std::sqrt(3.0) * size, down = 1.5 * size;
    return {across, down, across * (columns + (rows > 1 ? .5 : 0)), down * (rows - 1) + 2 * size};
}
}

// A generator's handles at its values (in its definition's order).
inline std::vector<Handle> handles(const GeneratorDefinition& definition, std::span<const double> raw) {
    using namespace handle_detail;
    const auto values = settle(definition, raw);
    const auto value = [&](std::size_t index) { return values[index]; };
    const auto count = [&](std::size_t index) { return static_cast<int>(values[index]); };
    const auto name = [&](std::size_t index) { return label(definition, index); };
    std::vector<Handle> result;
    switch (definition.kind) {
        case GeneratorKind::rectangle: {
            const auto w = value(0) / 2, h = value(1) / 2;
            result.push_back(point({w, h, 0}, {0, 1}, "Size"));
            result.push_back(point(roundnessPoint({w, h, 0}, {w, -h, 0}, {-w, h, 0}, value(2)), {2}, name(2)));
            break;
        }
        case GeneratorKind::ellipse:
            result.push_back(point({value(0) / 2, 0, 0}, {0}, name(0)));
            result.push_back(point({0, value(1) / 2, 0}, {1}, name(1)));
            break;
        case GeneratorKind::polygon: {
            const auto n = count(0);
            const auto corner = polar(value(1), 90), next = polar(value(1), 90 + 360.0 / n), previous = polar(value(1), 90 - 360.0 / n);
            result.push_back(point(corner, {1}, name(1), 0));
            result.push_back(point(roundnessPoint(corner, previous, next, value(2)), {2}, name(2)));
            break;
        }
        case GeneratorKind::star: {
            const auto corners = starCorners(count(0), value(1), value(2));
            result.push_back(point(corners[0], {1}, name(1), 0));
            result.push_back(point(corners[1], {2}, name(2)));
            result.push_back(point(roundnessPoint(corners[0], corners.back(), corners[1], value(3)), {3}, name(3)));
            break;
        }
        case GeneratorKind::ring:
            result.push_back(point(polar(value(0), 45), {0}, name(0)));
            result.push_back(point(polar(value(1), 45), {1}, name(1)));
            break;
        case GeneratorKind::arc:
            result.push_back(point(polar(value(0), value(1)), {1}, name(1)));
            result.push_back(point(polar(value(0), value(1) + value(2)), {2}, name(2)));
            result.push_back(point(polar(value(0), value(1) + value(2) / 2), {0}, name(0)));
            break;
        case GeneratorKind::spiral:
            result.push_back(point(polar(value(2), 90 + 360 * value(0)), {2, 0}, "Outer end", 0));
            result.push_back(point(polar(value(1), 90), {1}, name(1)));
            break;
        case GeneratorKind::lissajous: {
            const auto size = value(3);
            result.push_back(point({size, size, 0}, {3}, name(3)));
            result.push_back(point({size * std::sin(value(2) * pi / 180), 0, 0}, {2}, name(2)));
            // Each frequency's count on an edge of its own, clear of the phase.
            result.push_back(counter({0, -size, 0}, 0, name(0)));
            result.push_back(counter({-size, 0, 0}, 1, name(1)));
            break;
        }
        case GeneratorKind::spirograph:
            result.push_back(point({value(2), 0, 0}, {2}, name(2), 0));
            result.push_back(point({-value(2) * spirographInner(value(0), value(1)), 0, 0}, {1}, name(1)));
            break;
        case GeneratorKind::rose:
            result.push_back(point({0, value(1), 0}, {1}, name(1), 0));
            break;
        case GeneratorKind::wave: {
            const auto cycles = value(0), amplitude = value(1), length = value(2), phase = value(3) * pi / 180;
            const auto height = [&](double t) { return amplitude * std::sin(2 * pi * cycles * t + phase); };
            result.push_back(point({length / 2, height(1), 0}, {2}, name(2)));
            const auto crest = waveCrest(cycles, value(3));
            if (crest.has_value()) { result.push_back(point({length * (*crest - .5), amplitude, 0}, {1, 3}, "Amplitude and phase")); }
            result.push_back(counter({-length / 2, height(0), 0}, 0, name(0)));
            break;
        }
        case GeneratorKind::grid: {
            const auto w = value(2) / 2, h = value(3) / 2;
            result.push_back(point({w, h, 0}, {2, 3}, "Size"));
            result.push_back(counter({w, 0, 0}, 0, name(0)));
            result.push_back(counter({0, h, 0}, 1, name(1)));
            break;
        }
        case GeneratorKind::polarGrid:
            result.push_back(point({0, value(2), 0}, {2}, name(2), 1));
            result.push_back(counter({-value(2) / std::max(1, count(0)), 0, 0}, 0, name(0)));
            break;
        case GeneratorKind::hexGrid: {
            const auto columns = count(0), rows = count(1);
            const auto size = value(2);
            const auto layout = hex(columns, rows, size);
            result.push_back(point(layout.centre(0, 0, size) + Vec3 {0, size, 0}, {2}, name(2)));
            result.push_back(counter(layout.centre(0, columns - 1, size) + Vec3 {layout.across / 2, 0, 0}, 0, name(0)));
            result.push_back(counter(layout.centre(rows - 1, 0, size) - Vec3 {0, size, 0}, 1, name(1)));
            break;
        }
        case GeneratorKind::box:
            // Widths on the left, clear of the transform gizmo's X arrow.
            result.push_back(point({-value(0) / 2, 0, 0}, {0}, name(0), std::nullopt, Vec3 {1, 0, 0}));
            result.push_back(point({0, value(1) / 2, 0}, {1}, name(1), std::nullopt, Vec3 {0, 1, 0}));
            result.push_back(point({0, 0, value(2) / 2}, {2}, name(2), std::nullopt, Vec3 {0, 0, 1}));
            break;
        case GeneratorKind::sphere:
            result.push_back(point({-value(0), 0, 0}, {0}, name(0), 1, Vec3 {1, 0, 0}));
            result.push_back(counter({0, value(0), 0}, 2, name(2)));
            break;
        case GeneratorKind::torus:
            result.push_back(point({-value(0), 0, 0}, {0}, name(0), 2, Vec3 {1, 0, 0}));
            result.push_back(point({-value(0), value(1), 0}, {1}, name(1), 3, Vec3 {0, 1, 0}));
            break;
        case GeneratorKind::cylinder:
            result.push_back(point({-value(0), 0, 0}, {0}, name(0), 2, Vec3 {1, 0, 0}));
            result.push_back(point({0, value(1) / 2, 0}, {1}, name(1), std::nullopt, Vec3 {0, 1, 0}));
            break;
        case GeneratorKind::cone:
            result.push_back(point({-value(0), -value(1) / 2, 0}, {0}, name(0), 2, Vec3 {1, 0, 0}));
            result.push_back(point({0, value(1) / 2, 0}, {1}, name(1), std::nullopt, Vec3 {0, 1, 0}));
            break;
    }
    return result;
}

// A point handle dragged on from `previous` to `to` (own space): the values
// then, from the values now. With `constrained` (Shift), a size keeps its
// proportions.
inline std::vector<double> drag(const GeneratorDefinition& definition, std::size_t index, std::span<const double> raw, Vec3 previous, Vec3 to, bool constrained) {
    using namespace handle_detail;
    auto values = settle(definition, raw);
    const auto value = [&](std::size_t parameter) { return values[parameter]; };
    // Both halves of a size from a corner, in proportion when constrained.
    const auto size = [&](std::size_t width, std::size_t height) {
        auto w = 2 * std::abs(to.x), h = 2 * std::abs(to.y);
        if (constrained && value(width) > 0 && value(height) > 0) {
            const auto scale = std::max(w / value(width), h / value(height));
            w = value(width) * scale;
            h = value(height) * scale;
        }
        values[width] = w;
        values[height] = h;
    };
    switch (definition.kind) {
        case GeneratorKind::rectangle: {
            const auto w = value(0) / 2, h = value(1) / 2;
            if (index == 0) {
                size(0, 1);
            } else {
                values[2] = roundnessAt({w, h, 0}, {w, -h, 0}, {-w, h, 0}, to);
            }
            break;
        }
        case GeneratorKind::ellipse:
            if (constrained && value(0) > 0 && value(1) > 0) {
                const auto scale = (index == 0 ? 2 * std::abs(to.x) / value(0) : 2 * std::abs(to.y) / value(1));
                values[0] *= scale;
                values[1] *= scale;
            } else if (index == 0) {
                values[0] = 2 * std::abs(to.x);
            } else {
                values[1] = 2 * std::abs(to.y);
            }
            break;
        case GeneratorKind::polygon: {
            if (index == 0) {
                values[1] = flatLength(to);
            } else {
                const auto n = static_cast<int>(value(0));
                values[2] = roundnessAt(polar(value(1), 90), polar(value(1), 90 - 360.0 / n), polar(value(1), 90 + 360.0 / n), to);
            }
            break;
        }
        case GeneratorKind::star: {
            if (index == 0) {
                values[1] = flatLength(to);
            } else if (index == 1) {
                values[2] = flatLength(to);
            } else {
                const auto corners = starCorners(static_cast<int>(value(0)), value(1), value(2));
                values[3] = roundnessAt(corners[0], corners.back(), corners[1], to);
            }
            break;
        }
        case GeneratorKind::ring:
            values[index] = flatLength(to);
            break;
        case GeneratorKind::arc:
            if (index == 0) {
                // The start moves; the end stays.
                const auto delta = turn(degreesOf(previous), degreesOf(to));
                values[1] = value(1) + delta;
                values[2] = std::clamp(value(2) - delta, 0.0, 360.0);
            } else if (index == 1) {
                values[2] = std::clamp(value(2) + turn(degreesOf(previous), degreesOf(to)), 0.0, 360.0);
            } else {
                values[0] = flatLength(to);
            }
            break;
        case GeneratorKind::spiral:
            if (index == 0) {
                // Out or in for its radius; round for its turns.
                values[2] = flatLength(to);
                values[0] = value(0) + turn(degreesOf(previous), degreesOf(to)) / 360;
            } else {
                values[1] = flatLength(to);
            }
            break;
        case GeneratorKind::lissajous:
            if (index == 0) {
                values[3] = std::max(std::abs(to.x), std::abs(to.y));
            } else if (value(3) > 0) {
                // Across the whole figure is half a turn of phase.
                values[2] = value(2) + (to.x - previous.x) / (2 * value(3)) * 180;
            }
            break;
        case GeneratorKind::spirograph:
            if (index == 0) {
                values[2] = flatLength(to);
            } else if (value(2) > 0 && value(0) > 0) {
                const auto k = std::clamp(-to.x / value(2), -1.0, 1.0);
                const auto rolling = ringTeeth - value(0);
                values[1] = rolling * (1 - k) / (1 + k) / value(0) * 100;
            }
            break;
        case GeneratorKind::rose:
            values[1] = flatLength(to);
            break;
        case GeneratorKind::wave:
            if (index == 0) {
                values[2] = 2 * std::abs(to.x);
            } else if (value(2) > 0) {
                // Up and down for its height; along for where its crest falls.
                values[1] = std::abs(to.y);
                values[3] = value(3) - (to.x - previous.x) / value(2) * 360 * value(0);
                while (values[3] > 360) { values[3] -= 360; }
                while (values[3] < -360) { values[3] += 360; }
            }
            break;
        case GeneratorKind::grid:
            size(2, 3);
            break;
        case GeneratorKind::polarGrid:
            values[2] = flatLength(to);
            break;
        case GeneratorKind::hexGrid: {
            // Every cell grows from the middle, so the size follows the
            // handle's distance from it.
            const auto at = handles(definition, values)[0].at;
            if (flatLength(at) > 0) { values[2] = value(2) * flatLength(to) / flatLength(at); }
            break;
        }
        case GeneratorKind::box:
            values[index] = 2 * std::abs(index == 0 ? to.x : index == 1 ? to.y : to.z);
            break;
        case GeneratorKind::sphere:
            values[0] = std::abs(to.x);
            break;
        case GeneratorKind::torus:
            if (index == 0) {
                values[0] = std::abs(to.x);
            } else {
                values[1] = std::abs(to.y);
            }
            break;
        case GeneratorKind::cylinder:
        case GeneratorKind::cone:
            if (index == 0) {
                values[0] = std::abs(to.x);
            } else {
                values[1] = 2 * std::abs(to.y);
            }
            break;
    }
    return settle(definition, values);
}

// A count stepped by `steps` whole units.
inline std::vector<double> step(const GeneratorDefinition& definition, std::size_t parameter, std::span<const double> raw, int steps) {
    auto values = settle(definition, raw);
    if (parameter < values.size()) { values[parameter] = std::round(values[parameter]) + steps; }
    return settle(definition, values);
}
}
