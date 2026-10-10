#pragma once

#include "Generators.h"
#include "Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <numeric>
#include <span>
#include <vector>

// What a generator draws for a set of its numbers: strokes of lines and
// cubic curves, and for a solid its faces, which hide what is behind them.
// Pure geometry, so it can be tested alone.
namespace motion::generator {
// A straight line from points[0] to points[3], or a cubic curve through all
// four.
struct Segment {
    std::array<Vec3, 4> points;
    bool curve = false;
};
// Segments end to end, drawn in one stroke.
using Path = std::vector<Segment>;

struct Geometry {
    std::vector<Path> paths;
    // Closed flat shapes whose insides hide what is behind them, as an SVG's
    // fills do (a ring's hole hides nothing).
    bool filled = false;
    // A solid's faces, or a grid's: what hides behind it.
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

namespace detail {
constexpr double pi = std::numbers::pi;
// A cubic curve's handle length for a quarter circle of radius 1.
constexpr double circleHandle = 0.5522847498307936;

inline Segment line(Vec3 a, Vec3 b) { return {{a, a, b, b}, false}; }
// A flat convex face, fanned from its first corner, added to the geometry's.
inline void addFace(Geometry& geometry, std::initializer_list<Vec3> corners) {
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    geometry.vertices.insert(geometry.vertices.end(), corners.begin(), corners.end());
    for (std::uint32_t corner = 2; corner < corners.size(); ++corner) { geometry.triangles.push_back({first, first + corner - 1, first + corner}); }
}
inline Segment cubic(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { return {{a, b, c, d}, true}; }
inline Vec3 flat(double x, double y) { return {x, y, 0}; }
inline Vec3 polar(double radius, double degrees) { return flat(radius * std::cos(degrees * pi / 180), radius * std::sin(degrees * pi / 180)); }

// A stroke through points, closed back to the first if asked.
inline Path polyline(const std::vector<Vec3>& points, bool closed) {
    Path path;
    for (std::size_t index = 1; index < points.size(); ++index) { path.push_back(line(points[index - 1], points[index])); }
    if (closed && points.size() > 2) { path.push_back(line(points.back(), points.front())); }
    return path;
}

// An ellipse as four quarter curves, anticlockwise from the right unless
// turned round (which makes it a hole in a fill).
inline Path ellipse(Vec3 centre, double radiusX, double radiusY, bool clockwise = false) {
    const auto sign = clockwise ? -1.0 : 1.0;
    const auto at = [&](double x, double y) { return Vec3 {centre.x + x * radiusX, centre.y + sign * y * radiusY, centre.z}; };
    constexpr auto k = circleHandle;
    return {
        cubic(at(1, 0), at(1, k), at(k, 1), at(0, 1)),
        cubic(at(0, 1), at(-k, 1), at(-1, k), at(-1, 0)),
        cubic(at(-1, 0), at(-1, -k), at(-k, -1), at(0, -1)),
        cubic(at(0, -1), at(k, -1), at(1, -k), at(1, 0)),
    };
}

// A closed polygon whose corners are rounded by `roundness` (0 to 1): each
// corner is cut back by that share of half its shorter edge and the cut
// bridged by a curve that leaves and meets the edges along them.
inline Path roundedPolygon(const std::vector<Vec3>& corners, double roundness) {
    const auto count = corners.size();
    if (count < 3) { return {}; }
    if (!(roundness > 0)) { return polyline(corners, true); }
    struct Cut { Vec3 in, out, corner; };
    std::vector<Cut> cuts;
    cuts.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& previous = corners[(index + count - 1) % count];
        const auto& corner = corners[index];
        const auto& next = corners[(index + 1) % count];
        const auto toPrevious = previous - corner, toNext = next - corner;
        const auto reach = roundness * .5 * std::min(toPrevious.length(), toNext.length());
        const auto unit = [](Vec3 value) { return value.normalized(1.0e-12).value_or(Vec3 {}); };
        cuts.push_back({corner + unit(toPrevious) * reach, corner + unit(toNext) * reach, corner});
    }
    Path path;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& cut = cuts[index];
        const auto& next = cuts[(index + 1) % count];
        path.push_back(cubic(cut.in, cut.in + (cut.corner - cut.in) * circleHandle, cut.out + (cut.corner - cut.out) * circleHandle, cut.out));
        if ((next.in - cut.out).length() > 1.0e-12) { path.push_back(line(cut.out, next.in)); }
    }
    return path;
}

// Points of a closed or open curve, `steps` of them along it.
template <typename Point>
std::vector<Vec3> sampled(int steps, Point&& point) {
    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(steps) + 1);
    for (int step = 0; step <= steps; ++step) { points.push_back(point(static_cast<double>(step) / steps)); }
    return points;
}

// Straight segments joined into as few strokes as can be walked end to end:
// each stroke follows on from a segment that ends where it stops, so the
// beam jumps only where it must.
inline std::vector<Path> chained(const std::vector<std::pair<Vec3, Vec3>>& segments) {
    const auto key = [](Vec3 point) {
        const auto rounded = [](double value) { return static_cast<std::int64_t>(std::llround(value * 1.0e7)); };
        return std::array<std::int64_t, 3> {rounded(point.x), rounded(point.y), rounded(point.z)};
    };
    std::map<std::array<std::int64_t, 3>, std::vector<std::size_t>> ends;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        ends[key(segments[index].first)].push_back(index);
        ends[key(segments[index].second)].push_back(index);
    }
    std::vector<bool> used(segments.size(), false);
    std::vector<Path> paths;
    for (std::size_t start = 0; start < segments.size(); ++start) {
        if (used[start]) { continue; }
        used[start] = true;
        Path path {line(segments[start].first, segments[start].second)};
        auto tail = segments[start].second;
        while (true) {
            const auto& candidates = ends[key(tail)];
            const auto next = std::find_if(candidates.begin(), candidates.end(), [&](std::size_t index) { return !used[index]; });
            if (next == candidates.end()) { break; }
            used[*next] = true;
            const auto& segment = segments[*next];
            const auto forward = key(segment.first) == key(tail);
            const auto to = forward ? segment.second : segment.first;
            path.push_back(line(tail, to));
            tail = to;
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

// A solid from a grid of vertices (rows of `columns`, each row wrapping when
// asked): its faces, two triangles a cell.
inline void gridFaces(Geometry& geometry, std::uint32_t first, int rows, int columns, bool wrapColumns, bool wrapRows) {
    const auto at = [&](int row, int column) {
        return first + static_cast<std::uint32_t>(((row + rows) % rows) * columns + (column + columns) % columns);
    };
    for (int row = 0; row < (wrapRows ? rows : rows - 1); ++row) {
        for (int column = 0; column < (wrapColumns ? columns : columns - 1); ++column) {
            const auto a = at(row, column), b = at(row, column + 1), c = at(row + 1, column + 1), d = at(row + 1, column);
            geometry.triangles.push_back({a, b, c});
            geometry.triangles.push_back({a, c, d});
        }
    }
}
}

// The parameter values a definition's specs allow: clamped, counts whole.
inline std::vector<double> settle(const GeneratorDefinition& definition, std::span<const double> values) {
    std::vector<double> settled;
    settled.reserve(definition.parameters.size());
    for (std::size_t index = 0; index < definition.parameters.size(); ++index) {
        const auto& spec = definition.parameters[index];
        const auto value = index < values.size() && std::isfinite(values[index]) ? spec.clamp(values[index]) : spec.defaultValue;
        settled.push_back(spec.decimals == 0 && spec.step >= 1 ? std::round(value) : value);
    }
    return settled;
}

// What a generator draws for its parameters' values (in its definition's
// order; missing or out-of-range ones are settled first).
inline Geometry build(const GeneratorDefinition& definition, std::span<const double> raw) {
    using namespace detail;
    const auto values = settle(definition, raw);
    const auto value = [&](std::size_t index) { return values[index]; };
    const auto count = [&](std::size_t index) { return static_cast<int>(values[index]); };
    Geometry geometry;
    switch (definition.kind) {
        case GeneratorKind::rectangle: {
            const auto w = value(0) / 2, h = value(1) / 2;
            geometry.paths.push_back(roundedPolygon({flat(w, h), flat(-w, h), flat(-w, -h), flat(w, -h)}, value(2) / 100));
            geometry.filled = true;
            break;
        }
        case GeneratorKind::ellipse:
            geometry.paths.push_back(ellipse({}, value(0) / 2, value(1) / 2));
            geometry.filled = true;
            break;
        case GeneratorKind::polygon: {
            std::vector<Vec3> corners;
            for (int side = 0; side < count(0); ++side) { corners.push_back(polar(value(1), 90 + 360.0 * side / count(0))); }
            geometry.paths.push_back(roundedPolygon(corners, value(2) / 100));
            geometry.filled = true;
            break;
        }
        case GeneratorKind::star: {
            std::vector<Vec3> corners;
            for (int point = 0; point < 2 * count(0); ++point) { corners.push_back(polar(point % 2 == 0 ? value(1) : value(2), 90 + 180.0 * point / count(0))); }
            geometry.paths.push_back(roundedPolygon(corners, value(3) / 100));
            geometry.filled = true;
            break;
        }
        case GeneratorKind::ring: {
            // The inner edge turns the other way, so the fill has a hole.
            const auto outer = std::max(value(0), value(1)), inner = std::min(value(0), value(1));
            geometry.paths.push_back(ellipse({}, outer, outer));
            if (inner > 0) { geometry.paths.push_back(ellipse({}, inner, inner, true)); }
            geometry.filled = true;
            break;
        }
        case GeneratorKind::arc: {
            // Up to a quarter circle a curve.
            const auto radius = value(0), start = value(1), sweep = value(2);
            const auto pieces = std::max(1, static_cast<int>(std::ceil(sweep / 90 - 1.0e-9)));
            Path path;
            for (int piece = 0; piece < pieces; ++piece) {
                const auto from = start + sweep * piece / pieces, to = start + sweep * (piece + 1) / pieces;
                const auto handle = radius * 4.0 / 3.0 * std::tan((to - from) * pi / 720);
                const auto a = polar(radius, from), d = polar(radius, to);
                const auto tangentA = polar(1, from + 90), tangentD = polar(1, to - 90);
                path.push_back(cubic(a, a + tangentA * handle, d + tangentD * handle, d));
            }
            if (sweep > 0) { geometry.paths.push_back(std::move(path)); }
            break;
        }
        case GeneratorKind::spiral: {
            const auto turns = value(0), inner = value(1), outer = value(2);
            const auto steps = std::max(16, static_cast<int>(std::ceil(turns * 96)));
            if (turns > 0) { geometry.paths.push_back(polyline(sampled(steps, [&](double t) { return polar(inner + (outer - inner) * t, 90 + 360 * turns * t); }), false)); }
            break;
        }
        case GeneratorKind::lissajous: {
            const auto a = value(0), b = value(1), phase = value(2) * pi / 180, size = value(3);
            const auto steps = 128 * static_cast<int>(a + b);
            geometry.paths.push_back(polyline(sampled(steps, [&](double t) { return flat(size * std::sin(a * 2 * pi * t + phase), size * std::sin(b * 2 * pi * t)); }), false));
            break;
        }
        case GeneratorKind::spirograph: {
            // A gear of `teeth` rolling inside a ring of 96, its pen part way
            // out; it closes after as many turns as the ratio needs.
            constexpr int ringTeeth = 96;
            const auto teeth = count(0);
            const auto pen = value(1) / 100, size = value(2);
            const auto R = static_cast<double>(ringTeeth), r = static_cast<double>(teeth), d = pen * r;
            const auto turns = teeth / std::gcd(ringTeeth, teeth);
            const auto extent = std::max(1.0e-9, R - r + d);
            const auto steps = std::max(256, turns * 192);
            geometry.paths.push_back(polyline(sampled(steps, [&](double t) {
                const auto angle = 2 * pi * turns * t;
                const auto x = (R - r) * std::cos(angle) + d * std::cos((R - r) / r * angle);
                const auto y = (R - r) * std::sin(angle) - d * std::sin((R - r) / r * angle);
                return flat(size * x / extent, size * y / extent);
            }), false));
            break;
        }
        case GeneratorKind::rose: {
            // An odd count of petals traces in half a turn; an even one needs
            // a whole turn and draws twice as many, so it is halved first.
            const auto petals = count(0);
            const auto size = value(1);
            const auto k = petals % 2 == 0 ? petals / 2.0 : static_cast<double>(petals);
            const auto span = petals % 2 == 0 ? 2 * pi : pi;
            geometry.paths.push_back(polyline(sampled(std::max(128, petals * 96), [&](double t) {
                const auto angle = span * t + pi / 2;
                return polar(size * std::cos(k * (angle - pi / 2)), angle * 180 / pi);
            }), false));
            break;
        }
        case GeneratorKind::wave: {
            const auto cycles = value(0), amplitude = value(1), length = value(2), phase = value(3) * pi / 180;
            const auto steps = std::max(64, static_cast<int>(std::ceil(cycles * 48)));
            geometry.paths.push_back(polyline(sampled(steps, [&](double t) { return flat(length * (t - .5), amplitude * std::sin(2 * pi * cycles * t + phase)); }), false));
            break;
        }
        case GeneratorKind::grid: {
            // Rows back and forth, then columns back and forth: each line
            // once, each starting near where the last ended.
            const auto columns = count(0), rows = count(1);
            const auto w = value(2), h = value(3);
            for (int row = 0; row <= rows; ++row) {
                const auto y = -h / 2 + h * row / rows;
                const auto right = row % 2 == 0;
                geometry.paths.push_back({line(flat(right ? -w / 2 : w / 2, y), flat(right ? w / 2 : -w / 2, y))});
            }
            for (int column = 0; column <= columns; ++column) {
                const auto x = (rows % 2 == 0 ? w / 2 : -w / 2) + (rows % 2 == 0 ? -1 : 1) * w * column / columns;
                const auto down = column % 2 == 0;
                geometry.paths.push_back({line(flat(x, down ? h / 2 : -h / 2), flat(x, down ? -h / 2 : h / 2))});
            }
            // Its cells are solid, as a floor or a wall.
            addFace(geometry, {flat(-w / 2, -h / 2), flat(w / 2, -h / 2), flat(w / 2, h / 2), flat(-w / 2, h / 2)});
            break;
        }
        case GeneratorKind::polarGrid: {
            const auto rings = count(0), spokes = count(1);
            const auto radius = value(2);
            for (int ring = 1; ring <= rings; ++ring) { geometry.paths.push_back(ellipse({}, radius * ring / rings, radius * ring / rings)); }
            for (int spoke = 0; spoke < spokes; ++spoke) {
                const auto outward = spoke % 2 == 0;
                const auto tip = polar(radius, 90 + 360.0 * spoke / spokes);
                geometry.paths.push_back({outward ? line({}, tip) : line(tip, {})});
            }
            // A solid disc, its edge within a hair of the outer ring.
            constexpr int around = 96;
            const auto centre = static_cast<std::uint32_t>(geometry.vertices.size());
            geometry.vertices.push_back({});
            for (int step = 0; step < around; ++step) { geometry.vertices.push_back(polar(radius, 360.0 * step / around)); }
            for (std::uint32_t step = 0; step < around; ++step) { geometry.triangles.push_back({centre, centre + 1 + step, centre + 1 + (step + 1) % around}); }
            break;
        }
        case GeneratorKind::hexGrid: {
            // Pointy-topped cells in offset rows, each shared edge once.
            const auto columns = count(0), rows = count(1);
            const auto size = value(2);
            const auto across = std::sqrt(3.0) * size, down = 1.5 * size;
            const auto width = across * (columns + (rows > 1 ? .5 : 0)), height = down * (rows - 1) + 2 * size;
            std::vector<std::pair<Vec3, Vec3>> edges;
            std::map<std::array<std::int64_t, 6>, bool> seen;
            const auto key = [](Vec3 a, Vec3 b) {
                const auto rounded = [](double value) { return static_cast<std::int64_t>(std::llround(value * 1.0e7)); };
                std::array<std::int64_t, 3> first {rounded(a.x), rounded(a.y), rounded(a.z)}, second {rounded(b.x), rounded(b.y), rounded(b.z)};
                if (second < first) { std::swap(first, second); }
                return std::array<std::int64_t, 6> {first[0], first[1], first[2], second[0], second[1], second[2]};
            };
            for (int row = 0; row < rows; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const auto centre = flat(-width / 2 + across / 2 + across * column + (row % 2 == 1 ? across / 2 : 0), height / 2 - size - down * row);
                    for (int corner = 0; corner < 6; ++corner) {
                        const auto a = centre + polar(size, 90 + 60.0 * corner), b = centre + polar(size, 90 + 60.0 * (corner + 1));
                        if (!seen.emplace(key(a, b), true).second) { continue; }
                        edges.emplace_back(a, b);
                    }
                    // Each cell solid.
                    const auto at = [&](int corner) { return centre + polar(size, 90 + 60.0 * corner); };
                    addFace(geometry, {at(0), at(1), at(2), at(3), at(4), at(5)});
                }
            }
            geometry.paths = chained(edges);
            break;
        }
        case GeneratorKind::box: {
            const auto w = value(0) / 2, h = value(1) / 2, d = value(2) / 2;
            geometry.vertices = {{-w, -h, -d}, {w, -h, -d}, {w, h, -d}, {-w, h, -d}, {-w, -h, d}, {w, -h, d}, {w, h, d}, {-w, h, d}};
            const std::array<std::array<std::uint32_t, 4>, 6> faces {{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}};
            for (const auto& face : faces) {
                geometry.triangles.push_back({face[0], face[1], face[2]});
                geometry.triangles.push_back({face[0], face[2], face[3]});
            }
            const std::array<std::array<int, 2>, 12> edges {{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            std::vector<std::pair<Vec3, Vec3>> segments;
            for (const auto& edge : edges) { segments.emplace_back(geometry.vertices[static_cast<std::size_t>(edge[0])], geometry.vertices[static_cast<std::size_t>(edge[1])]); }
            geometry.paths = chained(segments);
            break;
        }
        case GeneratorKind::sphere: {
            // Parallels between the poles, meridians from pole to pole, on
            // the same vertices as the faces.
            const auto radius = value(0);
            const auto segments = count(1), rings = count(2);
            const auto at = [&](int ring, int segment) {
                const auto latitude = pi * ring / rings - pi / 2, longitude = 2 * pi * segment / segments;
                return Vec3 {radius * std::cos(latitude) * std::cos(longitude), radius * std::sin(latitude), radius * std::cos(latitude) * std::sin(longitude)};
            };
            for (int ring = 0; ring <= rings; ++ring) {
                for (int segment = 0; segment < segments; ++segment) { geometry.vertices.push_back(at(ring, segment)); }
            }
            detail::gridFaces(geometry, 0, rings + 1, segments, true, false);
            for (int ring = 1; ring < rings; ++ring) {
                std::vector<Vec3> parallel;
                for (int segment = 0; segment < segments; ++segment) { parallel.push_back(at(ring, segment)); }
                geometry.paths.push_back(polyline(parallel, true));
            }
            for (int segment = 0; segment < segments; ++segment) {
                std::vector<Vec3> meridian;
                for (int ring = 0; ring <= rings; ++ring) { meridian.push_back(at(segment % 2 == 0 ? ring : rings - ring, segment)); }
                geometry.paths.push_back(polyline(meridian, false));
            }
            break;
        }
        case GeneratorKind::torus: {
            const auto radius = value(0), thickness = value(1);
            const auto segments = count(2), sides = count(3);
            const auto at = [&](int segment, int side) {
                const auto around = 2 * pi * segment / segments, about = 2 * pi * side / sides;
                const auto distance = radius + thickness * std::cos(about);
                return Vec3 {distance * std::cos(around), thickness * std::sin(about), distance * std::sin(around)};
            };
            for (int segment = 0; segment < segments; ++segment) {
                for (int side = 0; side < sides; ++side) { geometry.vertices.push_back(at(segment, side)); }
            }
            detail::gridFaces(geometry, 0, segments, sides, true, true);
            for (int segment = 0; segment < segments; ++segment) {
                std::vector<Vec3> loop;
                for (int side = 0; side < sides; ++side) { loop.push_back(at(segment, side)); }
                geometry.paths.push_back(polyline(loop, true));
            }
            for (int side = 0; side < sides; ++side) {
                std::vector<Vec3> loop;
                for (int segment = 0; segment < segments; ++segment) { loop.push_back(at(segment, side)); }
                geometry.paths.push_back(polyline(loop, true));
            }
            break;
        }
        case GeneratorKind::cylinder:
        case GeneratorKind::cone: {
            // Rims round the ends (one for a cone, which comes to a point),
            // and a line up the side at each segment.
            const auto cone = definition.kind == GeneratorKind::cone;
            const auto radius = value(0), height = value(1);
            const auto segments = count(2);
            const auto rim = [&](int segment, double y, double r) { return Vec3 {r * std::cos(2 * pi * segment / segments), y, r * std::sin(2 * pi * segment / segments)}; };
            for (int segment = 0; segment < segments; ++segment) { geometry.vertices.push_back(rim(segment, -height / 2, radius)); }
            for (int segment = 0; segment < segments; ++segment) { geometry.vertices.push_back(rim(segment, height / 2, cone ? 0 : radius)); }
            const auto n = static_cast<std::uint32_t>(segments);
            geometry.vertices.push_back({0, -height / 2, 0});
            geometry.vertices.push_back({0, height / 2, 0});
            for (std::uint32_t segment = 0; segment < n; ++segment) {
                const auto next = (segment + 1) % n;
                geometry.triangles.push_back({segment, next, n + next});
                geometry.triangles.push_back({segment, n + next, n + segment});
                geometry.triangles.push_back({2 * n, next, segment});
                if (!cone) { geometry.triangles.push_back({2 * n + 1, n + segment, n + next}); }
            }
            std::vector<Vec3> bottom, top;
            for (int segment = 0; segment < segments; ++segment) {
                bottom.push_back(rim(segment, -height / 2, radius));
                top.push_back(rim(segment, height / 2, radius));
            }
            geometry.paths.push_back(polyline(bottom, true));
            if (!cone) { geometry.paths.push_back(polyline(top, true)); }
            for (int segment = 0; segment < segments; ++segment) {
                const auto low = rim(segment, -height / 2, radius), high = cone ? Vec3 {0, height / 2, 0} : rim(segment, height / 2, radius);
                geometry.paths.push_back({segment % 2 == 0 ? line(low, high) : line(high, low)});
            }
            break;
        }
        // Lights: pictures for the Scene, never drawn on the Scope.
        case GeneratorKind::ambientLight:
        case GeneratorKind::pointLight: {
            // A disc with rays all round (a point light's in three dimensions,
            // round a ball of three rings), reaching further the brighter it is.
            const auto reach = light_glyph::rayReach(value(0));
            constexpr auto disc = light_glyph::disc;
            if (definition.kind == GeneratorKind::ambientLight) {
                geometry.paths.push_back(ellipse({}, disc, disc));
                for (int ray = 0; ray < 8; ++ray) { geometry.paths.push_back({line(polar(1.4 * disc, 45.0 * ray), polar(reach, 45.0 * ray))}); }
                break;
            }
            const auto ring = [&](int axis) {
                return polyline(sampled(48, [&](double t) {
                    const auto a = disc * std::cos(2 * pi * t), b = disc * std::sin(2 * pi * t);
                    return axis == 0 ? Vec3 {a, b, 0} : axis == 1 ? Vec3 {a, 0, b} : Vec3 {0, a, b};
                }), false);
            };
            for (int axis = 0; axis < 3; ++axis) { geometry.paths.push_back(ring(axis)); }
            for (const auto direction : {Vec3 {1, 0, 0}, Vec3 {-1, 0, 0}, Vec3 {0, 1, 0}, Vec3 {0, -1, 0}, Vec3 {0, 0, 1}, Vec3 {0, 0, -1}}) {
                geometry.paths.push_back({line(direction * (1.4 * disc), direction * reach)});
            }
            break;
        }
        case GeneratorKind::directionalLight: {
            // A disc facing the way it shines, and parallel arrows from it,
            // longer the brighter it is.
            constexpr auto disc = light_glyph::disc;
            const auto reach = light_glyph::arrowReach(value(0));
            geometry.paths.push_back(ellipse({}, disc, disc));
            for (const auto from : {Vec3 {}, polar(.6 * disc, 0), polar(.6 * disc, 90), polar(.6 * disc, 180), polar(.6 * disc, 270)}) {
                const auto tip = from + Vec3 {0, 0, -reach};
                geometry.paths.push_back({line(from, tip), line(tip, tip + Vec3 {.025, 0, .045})});
                geometry.paths.push_back({line(tip, tip + Vec3 {-.025, 0, .045})});
            }
            break;
        }
        case GeneratorKind::spotLight: {
            // Its cone, with the ring inside which it is at full strength,
            // and its axis out to its intensity's handle.
            constexpr auto length = light_glyph::spotLength;
            const auto cone = value(2), softness = value(3) / 100;
            const auto rimAt = [&](double degrees) { return length * std::tan(degrees * pi / 360); };
            const auto ring = [&](double radius) {
                return polyline(sampled(48, [&](double t) { return Vec3 {radius * std::cos(2 * pi * t), radius * std::sin(2 * pi * t), -length}; }), false);
            };
            const auto outer = rimAt(cone), inner = rimAt(cone * (1 - softness));
            geometry.paths.push_back(ring(outer));
            if (softness > 0 && inner > 1.0e-3) { geometry.paths.push_back(ring(inner)); }
            for (int side = 0; side < 4; ++side) {
                const auto angle = pi / 2 * side;
                geometry.paths.push_back({line({}, {outer * std::cos(angle), outer * std::sin(angle), -length})});
            }
            geometry.paths.push_back({line({}, {0, 0, -light_glyph::spotReach(value(0))})});
            break;
        }
    }
    return geometry;
}
}
