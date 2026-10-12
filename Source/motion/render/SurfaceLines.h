#pragma once

#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

// Lines drawn on a surface: its outline as an eye sees it (where it turns
// from the eye) and patterns over it that show its shape: a spiral round it,
// slices through it, rings round its middle, contours of noise, a Hilbert
// curve laid on it. Each piece lies on one face, between two points given by
// their weights on its corners, so whoever draws it can put it wherever the
// face's corners go (the source's own space, or the scene). Pieces come in
// order along each line, end to end. Pure geometry, so it can be tested alone.
namespace motion::hidden::surface {
using Face = std::array<std::uint32_t, 3>;
using Weights = std::array<double, 3>;

// A stretch of line across one face.
struct Piece {
    std::uint32_t face;
    Weights from, to;
};

inline Vec3 at(const std::vector<Vec3>& corners, const Face& face, const Weights& weights) {
    return corners[face[0]] * weights[0] + corners[face[1]] * weights[1] + corners[face[2]] * weights[2];
}

enum class Pattern { spiral, hilbert, noise, slices, rings };

namespace detail {
// The box round the corners the faces use.
struct Bounds {
    Vec3 low, high;
    Vec3 middle() const { return (low + high) * .5; }
    double size() const { return std::max({high.x - low.x, high.y - low.y, high.z - low.z, 1.0e-12}); }
};
inline Bounds boundsOf(const std::vector<Vec3>& corners, const std::vector<Face>& faces) {
    Bounds bounds {{1.0e300, 1.0e300, 1.0e300}, {-1.0e300, -1.0e300, -1.0e300}};
    for (const auto& face : faces) {
        for (const auto corner : face) {
            const auto& p = corners[corner];
            bounds.low = {std::min(bounds.low.x, p.x), std::min(bounds.low.y, p.y), std::min(bounds.low.z, p.z)};
            bounds.high = {std::max(bounds.high.x, p.x), std::max(bounds.high.y, p.y), std::max(bounds.high.z, p.z)};
        }
    }
    return bounds;
}

// Where a line meets a side of a face: the side (its corners, lower first)
// and how far along it, so the face beyond finds the same place.
using Meeting = std::tuple<std::uint32_t, std::uint32_t, std::int64_t>;

// A piece found on a face, with where it meets the face's sides at either
// end (none for an end inside the face).
struct Found {
    Piece piece;
    std::array<std::optional<Meeting>, 2> ends;
};

inline std::optional<Meeting> meetingOf(const Face& face, const Weights& weights) {
    std::array<std::size_t, 2> on {};
    int count = 0;
    for (std::size_t corner = 0; corner < 3; ++corner) {
        if (weights[corner] > 0 && count < 2) { on[static_cast<std::size_t>(count++)] = corner; }
    }
    // At a corner: met there by whatever else ends at that corner.
    if (count == 1 && weights[on[0]] == 1) { return Meeting {face[on[0]], face[on[0]], 0}; }
    if (count != 2 || weights[3 - on[0] - on[1]] != 0) { return std::nullopt; }
    auto first = on[0], second = on[1];
    if (face[first] > face[second]) { std::swap(first, second); }
    return Meeting {face[first], face[second], std::llround(weights[second] * 1.0e9)};
}

// The faces with each corner as the first corner at its place: a mesh may
// repeat a place (a ball's poles, a seam), and lines meet there all the same.
inline std::vector<Face> byPlace(const std::vector<Vec3>& corners, const std::vector<Face>& faces) {
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::uint32_t> first;
    std::vector<Face> placed(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& p = corners[faces[index][corner]];
            placed[index][corner] = first.try_emplace({std::llround(p.x * 1.0e7), std::llround(p.y * 1.0e7), std::llround(p.z * 1.0e7)}, faces[index][corner]).first->second;
        }
    }
    return placed;
}

// Pieces put in order along their lines: each one's neighbour is the one
// meeting a side where it does. Loops go round once; lines go end to end.
inline std::vector<Piece> chained(std::vector<Found> found) {
    std::map<Meeting, std::vector<std::size_t>> at;
    for (std::size_t index = 0; index < found.size(); ++index) {
        for (const auto& end : found[index].ends) {
            if (end.has_value()) { at[*end].push_back(index); }
        }
    }
    // The other piece meeting `end`'s place, and which of its ends that is.
    const auto across = [&](std::size_t index, std::size_t end) -> std::optional<std::pair<std::size_t, std::size_t>> {
        const auto& meeting = found[index].ends[end];
        if (!meeting.has_value()) { return std::nullopt; }
        for (const auto other : at[*meeting]) {
            if (other == index) { continue; }
            return std::pair {other, found[other].ends[0] == meeting ? std::size_t {0} : std::size_t {1}};
        }
        return std::nullopt;
    };
    std::vector<char> used(found.size(), 0);
    std::vector<Piece> pieces;
    pieces.reserve(found.size());
    for (std::size_t start = 0; start < found.size(); ++start) {
        if (used[start] != 0) { continue; }
        // Back to where its line begins (or once round, for a loop), going
        // out of each piece by end `out`.
        auto first = start;
        std::size_t out = 0;
        for (std::size_t step = 0; step < found.size(); ++step) {
            const auto next = across(first, out);
            if (!next.has_value() || next->first == start || used[next->first] != 0) { break; }
            first = next->first;
            out = 1 - next->second;
        }
        // Then forward from there, coming in by end `out`.
        auto current = first;
        auto in = out;
        while (true) {
            used[current] = 1;
            auto piece = found[current].piece;
            if (in == 1) { std::swap(piece.from, piece.to); }
            pieces.push_back(piece);
            const auto next = across(current, 1 - in);
            if (!next.has_value() || used[next->first] != 0) { break; }
            current = next->first;
            in = next->second;
        }
    }
    return pieces;
}

// Where values over the faces (one per corner of each face, so a face can
// have its own: a spiral's angle is unwound face by face) cross each whole
// number, in order along each line.
inline std::vector<Piece> contours(const std::vector<Face>& faces, const std::vector<Face>& placed, const std::vector<std::array<double, 3>>& values) {
    // Levels nudged off whole numbers, so none passes exactly through a corner.
    constexpr double nudge = 1.0e-7;
    std::vector<Found> found;
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        const auto& v = values[index];
        // A face with no area (two corners at one place) draws nothing.
        const auto& p = placed[index];
        if (p[0] == p[1] || p[1] == p[2] || p[2] == p[0]) { continue; }
        const auto low = static_cast<long>(std::ceil(std::min({v[0], v[1], v[2]}) - nudge));
        const auto high = static_cast<long>(std::floor(std::max({v[0], v[1], v[2]}) - nudge));
        for (auto level = low; level <= high; ++level) {
            const auto value = static_cast<double>(level) + nudge;
            std::array<Weights, 2> ends {};
            int count = 0;
            for (std::size_t side = 0; side < 3 && count < 2; ++side) {
                const auto next = (side + 1) % 3;
                const auto a = v[side] - value, b = v[next] - value;
                if ((a < 0) == (b < 0)) { continue; }
                const auto share = a / (a - b);
                Weights weights {};
                weights[side] = 1 - share;
                weights[next] = share;
                ends[static_cast<std::size_t>(count++)] = weights;
            }
            if (count < 2) { continue; }
            found.push_back({{index, ends[0], ends[1]}, {meetingOf(placed[index], ends[0]), meetingOf(placed[index], ends[1])}});
        }
    }
    return chained(std::move(found));
}

// Smooth noise in space, about -1 to 1 (Perlin's, with a fixed table).
inline double noise(Vec3 point) {
    static const auto table = [] {
        std::array<int, 512> values {};
        std::uint32_t state = 2463534242u;
        std::array<int, 256> order {};
        for (int index = 0; index < 256; ++index) { order[static_cast<std::size_t>(index)] = index; }
        for (int index = 255; index > 0; --index) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            std::swap(order[static_cast<std::size_t>(index)], order[state % static_cast<std::uint32_t>(index + 1)]);
        }
        for (std::size_t index = 0; index < 512; ++index) { values[index] = order[index & 255]; }
        return values;
    }();
    const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const auto lerp = [](double t, double a, double b) { return a + t * (b - a); };
    const auto gradient = [](int hash, double x, double y, double z) {
        const auto h = hash & 15;
        const auto u = h < 8 ? x : y, v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    };
    const auto fx = std::floor(point.x), fy = std::floor(point.y), fz = std::floor(point.z);
    const auto X = static_cast<int>(fx) & 255, Y = static_cast<int>(fy) & 255, Z = static_cast<int>(fz) & 255;
    const auto x = point.x - fx, y = point.y - fy, z = point.z - fz;
    const auto u = fade(x), v = fade(y), w = fade(z);
    const auto& p = table;
    const auto A = p[static_cast<std::size_t>(X)] + Y, AA = p[static_cast<std::size_t>(A)] + Z, AB = p[static_cast<std::size_t>(A + 1)] + Z;
    const auto B = p[static_cast<std::size_t>(X + 1)] + Y, BA = p[static_cast<std::size_t>(B)] + Z, BB = p[static_cast<std::size_t>(B + 1)] + Z;
    const auto g = [&](int index, double gx, double gy, double gz) { return gradient(p[static_cast<std::size_t>(index)], gx, gy, gz); };
    return lerp(w, lerp(v, lerp(u, g(AA, x, y, z), g(BA, x - 1, y, z)), lerp(u, g(AB, x, y - 1, z), g(BB, x - 1, y - 1, z))),
        lerp(v, lerp(u, g(AA + 1, x, y, z - 1), g(BA + 1, x - 1, y, z - 1)), lerp(u, g(AB + 1, x, y - 1, z - 1), g(BB + 1, x - 1, y - 1, z - 1))));
}

// The point a Hilbert curve of `order` reaches at step `index`, in cells of
// a square `1 << order` across (the usual way, rotating quarters).
inline std::array<int, 2> hilbertPoint(int order, std::int64_t index) {
    int x = 0, y = 0;
    auto t = index;
    for (std::int64_t size = 1; size < (std::int64_t {1} << order); size *= 2) {
        const auto rx = static_cast<int>(1 & (t / 2));
        const auto ry = static_cast<int>(1 & (t ^ rx));
        if (ry == 0) {
            if (rx == 1) {
                x = static_cast<int>(size) - 1 - x;
                y = static_cast<int>(size) - 1 - y;
            }
            std::swap(x, y);
        }
        x += static_cast<int>(size) * rx;
        y += static_cast<int>(size) * ry;
        t /= 4;
    }
    return {x, y};
}
}

// A pattern over the faces, `across` lines across the surface's largest
// side. A spiral turns about the surface's own up (y) through its middle,
// slices run along it, and rings centre on its front (its nearest along z).
inline std::vector<Piece> pattern(const std::vector<Vec3>& corners, const std::vector<Face>& faces, Pattern kind, double across) {
    if (faces.empty() || !(across > 0)) { return {}; }
    const auto bounds = detail::boundsOf(corners, faces);
    const auto spacing = bounds.size() / across;
    const auto middle = bounds.middle();
    constexpr double turn = 2 * 3.14159265358979323846;
    if (kind == Pattern::hilbert) {
        // Laid on from the side each face looks to most, as a texture laid
        // on a box round it: a curve on each of its six sides, each face
        // taking the side it faces most squarely.
        const auto order = std::clamp(static_cast<int>(std::round(std::log2(across))), 1, 8);
        const auto cells = 1 << order;
        std::vector<Piece> pieces;
        for (std::size_t side = 0; side < 6; ++side) {
            const auto axis = side / 2;
            const auto toward = side % 2 == 0 ? 1.0 : -1.0;
            const auto u = (axis + 1) % 3, v = (axis + 2) % 3;
            const auto plane = [&](Vec3 point) { return std::array<double, 2> {(point[u] - bounds.low[u]) / bounds.size() * cells - .5, (point[v] - bounds.low[v]) / bounds.size() * cells - .5}; };
            // The faces turned most toward this axis, by the cells they cover.
            std::vector<std::uint32_t> facing;
            for (std::uint32_t index = 0; index < faces.size(); ++index) {
                const auto& f = faces[index];
                const auto normal = (corners[f[1]] - corners[f[0]]).cross(corners[f[2]] - corners[f[0]]);
                const std::array<double, 3> size {std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
                const auto most = size[0] >= size[1] && size[0] >= size[2] ? std::size_t {0} : size[1] >= size[2] ? std::size_t {1} : std::size_t {2};
                if (most == axis && normal[axis] * toward > 0) { facing.push_back(index); }
            }
            if (facing.empty()) { continue; }
            std::map<std::pair<int, int>, std::vector<std::uint32_t>> grid;
            for (const auto index : facing) {
                const auto& f = faces[index];
                const auto a = plane(corners[f[0]]), b = plane(corners[f[1]]), c = plane(corners[f[2]]);
                const auto x0 = static_cast<int>(std::floor(std::min({a[0], b[0], c[0]}))), x1 = static_cast<int>(std::ceil(std::max({a[0], b[0], c[0]})));
                const auto y0 = static_cast<int>(std::floor(std::min({a[1], b[1], c[1]}))), y1 = static_cast<int>(std::ceil(std::max({a[1], b[1], c[1]})));
                for (auto x = x0; x <= x1; ++x) {
                    for (auto y = y0; y <= y1; ++y) { grid[{x, y}].push_back(index); }
                }
            }
            // Each step of the curve, cut to each face it crosses there.
            const auto steps = std::int64_t {1} << (2 * order);
            auto previous = detail::hilbertPoint(order, 0);
            for (std::int64_t step = 1; step < steps; ++step) {
                const auto next = detail::hilbertPoint(order, step);
                const std::array<double, 2> p {static_cast<double>(previous[0]), static_cast<double>(previous[1])}, q {static_cast<double>(next[0]), static_cast<double>(next[1])};
                std::vector<std::uint32_t> near;
                for (const auto& key : {std::pair {previous[0], previous[1]}, std::pair {next[0], next[1]}}) {
                    const auto found = grid.find(key);
                    if (found != grid.end()) { near.insert(near.end(), found->second.begin(), found->second.end()); }
                }
                std::sort(near.begin(), near.end());
                near.erase(std::unique(near.begin(), near.end()), near.end());
                // The step's pieces in order along it.
                std::vector<std::pair<double, Piece>> along;
                for (const auto index : near) {
                    const auto& f = faces[index];
                    const auto a = plane(corners[f[0]]), b = plane(corners[f[1]]), c = plane(corners[f[2]]);
                    // The step's span inside the face, by its three sides.
                    const auto area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
                    // Seen edge on from this side (or a sliver): another side has it.
                    if (std::abs(area) < 1.0e-9) { continue; }
                    const auto weightsAt = [&](const std::array<double, 2>& point) {
                        const auto wb = ((point[0] - a[0]) * (c[1] - a[1]) - (point[1] - a[1]) * (c[0] - a[0])) / area;
                        const auto wc = ((b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0])) / area;
                        return Weights {1 - wb - wc, wb, wc};
                    };
                    const auto wp = weightsAt(p), wq = weightsAt(q);
                    double low = 0, high = 1;
                    for (std::size_t corner = 0; corner < 3 && low < high; ++corner) {
                        const auto start = wp[corner], end = wq[corner];
                        if (start < 0 && end < 0) { high = -1; break; }
                        if (start < 0) { low = std::max(low, start / (start - end)); }
                        if (end < 0) { high = std::min(high, start / (start - end)); }
                    }
                    if (!(high > low)) { continue; }
                    const auto mix = [&](double share) {
                        Weights weights {};
                        for (std::size_t corner = 0; corner < 3; ++corner) { weights[corner] = wp[corner] + (wq[corner] - wp[corner]) * share; }
                        return weights;
                    };
                    along.emplace_back(low, Piece {index, mix(low), mix(high)});
                }
                std::sort(along.begin(), along.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (const auto& [where, piece] : along) { pieces.push_back(piece); }
                previous = next;
            }
        }
        return pieces;
    }
    std::vector<std::array<double, 3>> values(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const auto& f = faces[index];
        // A spiral's angle round the face, unwound from its first corner so
        // it doesn't jump a turn across the face.
        std::array<double, 3> angles {};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto p = corners[f[corner]] - middle;
            angles[corner] = std::atan2(p.z, p.x);
            if (corner > 0) { angles[corner] = angles[0] + std::remainder(angles[corner] - angles[0], turn); }
        }
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto p = corners[f[corner]] - middle;
            switch (kind) {
                // Half a spacing in from the ends, so none is lost at a tip.
                case Pattern::slices: values[index][corner] = (p.y + middle.y - bounds.low.y) / spacing + .5; break;
                // Round its front (its middle, at its nearest along z): a bullseye seen from in front.
                case Pattern::rings: values[index][corner] = (p - Vec3 {0, 0, bounds.high.z - middle.z}).length() / spacing + .5; break;
                case Pattern::noise: values[index][corner] = 2.5 * detail::noise(p * (across / bounds.size() * .5) + Vec3 {17.3, 5.1, 9.7}); break;
                case Pattern::spiral:
                default: values[index][corner] = (p.y + bounds.size() / 2) / spacing - angles[corner] / turn; break;
            }
        }
    }
    return detail::contours(faces, detail::byPlace(corners, faces), values);
}

// Where faces turn from the eye: within a smooth stretch of surface (faces
// meeting at less than `crease`, radians), where the corners' normals,
// shared with their smooth neighbours, turn edge on to it, so a curved
// surface's outline is smooth; at a sharper edge between a face toward the
// eye and one away, along that edge; and along every edge of the surface
// only one face has (a flat drawing's outline).
inline std::vector<Piece> outline(const std::vector<Vec3>& corners, const std::vector<Face>& faces, Vec3 eye, double crease = .7) {
    std::vector<Vec3> normals(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const auto& f = faces[index];
        normals[index] = (corners[f[1]] - corners[f[0]]).cross(corners[f[2]] - corners[f[0]]).normalized(1.0e-300).value_or(Vec3 {});
    }
    // Faces by the corners they share (by place: a mesh may repeat a corner).
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::vector<std::uint32_t>> around;
    const auto keyOf = [](Vec3 p) { return std::tuple {std::llround(p.x * 1.0e7), std::llround(p.y * 1.0e7), std::llround(p.z * 1.0e7)}; };
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        const auto a = keyOf(corners[faces[index][0]]), b = keyOf(corners[faces[index][1]]), c = keyOf(corners[faces[index][2]]);
        if (a == b || b == c || c == a) { continue; }
        for (const auto corner : faces[index]) { around[keyOf(corners[corner])].push_back(index); }
    }
    const auto cosCrease = std::cos(crease);
    // Each face's corners' normals: its smooth neighbours' at that corner, together.
    std::vector<std::array<double, 3>> facing(faces.size());
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        const auto& f = faces[index];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            Vec3 sum;
            for (const auto other : around[keyOf(corners[f[corner]])]) {
                if (normals[other].dot(normals[index]) >= cosCrease) { sum = sum + normals[other]; }
            }
            const auto normal = sum.normalized(1.0e-300).value_or(normals[index]);
            facing[index][corner] = normal.dot((eye - corners[f[corner]]).normalized(1.0e-300).value_or(Vec3 {}));
        }
    }
    const auto placed = detail::byPlace(corners, faces);
    std::vector<detail::Found> found;
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        // A face with no area (two corners at one place) has no outline.
        const auto& p = placed[index];
        if (p[0] == p[1] || p[1] == p[2] || p[2] == p[0]) { continue; }
        const auto& v = facing[index];
        std::array<Weights, 2> ends {};
        int count = 0;
        for (std::size_t side = 0; side < 3 && count < 2; ++side) {
            const auto next = (side + 1) % 3;
            if ((v[side] < 0) == (v[next] < 0)) { continue; }
            const auto share = v[side] / (v[side] - v[next]);
            Weights weights {};
            weights[side] = 1 - share;
            weights[next] = share;
            ends[static_cast<std::size_t>(count++)] = weights;
        }
        if (count == 2) { found.push_back({{index, ends[0], ends[1]}, {detail::meetingOf(placed[index], ends[0]), detail::meetingOf(placed[index], ends[1])}}); }
    }
    // Sharp edges between a face toward the eye and one away, and edges
    // just one face has.
    std::map<std::pair<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::tuple<std::int64_t, std::int64_t, std::int64_t>>, std::vector<std::pair<std::uint32_t, std::size_t>>> edges;
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        const auto& p = placed[index];
        if (p[0] == p[1] || p[1] == p[2] || p[2] == p[0]) { continue; }
        for (std::size_t side = 0; side < 3; ++side) {
            auto a = keyOf(corners[faces[index][side]]), b = keyOf(corners[faces[index][(side + 1) % 3]]);
            if (b < a) { std::swap(a, b); }
            edges[{a, b}].push_back({index, side});
        }
    }
    for (const auto& [key, sharing] : edges) {
        if (std::get<0>(key) == std::get<1>(key)) { continue; }
        const auto [index, side] = sharing.front();
        const auto toward = [&](std::uint32_t face) { return normals[face].dot(eye - corners[faces[face][0]]) > 0; };
        bool drawn = sharing.size() == 1;
        if (sharing.size() == 2) {
            const auto other = sharing[1].first;
            drawn = toward(index) != toward(other) && normals[index].dot(normals[other]) < cosCrease;
        }
        if (!drawn) { continue; }
        Weights from {}, to {};
        from[side] = 1;
        to[(side + 1) % 3] = 1;
        found.push_back({{index, from, to}, {detail::meetingOf(placed[index], from), detail::meetingOf(placed[index], to)}});
    }
    return detail::chained(std::move(found));
}
}
