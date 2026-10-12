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

// A corner's place, to the nearest ten millionth.
inline std::tuple<std::int64_t, std::int64_t, std::int64_t> placeOf(Vec3 p) { return {std::llround(p.x * 1.0e7), std::llround(p.y * 1.0e7), std::llround(p.z * 1.0e7)}; }

// The faces with each corner as the first corner at its place: a mesh may
// repeat a place (a ball's poles, a seam), and lines meet there all the same.
inline std::vector<Face> byPlace(const std::vector<Vec3>& corners, const std::vector<Face>& faces) {
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::uint32_t> first;
    std::vector<Face> placed(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            placed[index][corner] = first.try_emplace(placeOf(corners[faces[index][corner]]), faces[index][corner]).first->second;
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
// `turn`: how far round its loop noise has morphed (a whole turn brings it
// back as it was).
inline std::vector<Piece> pattern(const std::vector<Vec3>& corners, const std::vector<Face>& faces, Pattern kind, double across, double turn = 0) {
    if (faces.empty() || !(across > 0)) { return {}; }
    const auto bounds = detail::boundsOf(corners, faces);
    const auto spacing = bounds.size() / across;
    const auto middle = bounds.middle();
    constexpr double whole = 2 * 3.14159265358979323846;
    // Noise morphs as three fields mixed by a point going round a circle of
    // the sphere of their mixes, tipped off its middle: as strong all the way
    // round, and never at its opposite (which would draw the same lines), so
    // it comes back only after a whole turn.
    constexpr double tip = .5;
    const std::array<double, 3> mix {std::cos(tip) * std::cos(whole * turn), std::cos(tip) * std::sin(whole * turn), std::sin(tip)};
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
    // The pattern's value at a point: its lines are where it is whole. A
    // spiral's angle is unwound from `from`, so it doesn't jump a turn across
    // a face, and on its axis, where it has none, is `from`.
    const auto valueAt = [&](Vec3 point, double from) {
        const auto p = point - middle;
        const auto onAxis = std::hypot(p.x, p.z) < 1.0e-9 * bounds.size();
        switch (kind) {
            // Half a spacing in from the ends, so none is lost at a tip.
            case Pattern::slices: return (p.y + middle.y - bounds.low.y) / spacing + .5;
            // Round its front (its middle, at its nearest along z): a bullseye seen from in front.
            case Pattern::rings: return (p - Vec3 {0, 0, bounds.high.z - middle.z}).length() / spacing + .5;
            case Pattern::noise: {
                const auto at = p * (across / bounds.size() * .5);
                return 2.5 * (mix[0] * detail::noise(at + Vec3 {17.3, 5.1, 9.7}) + mix[1] * detail::noise(at + Vec3 {-41.7, 23.9, 3.3}) + mix[2] * detail::noise(at + Vec3 {8.1, -29.3, 61.7}));
            }
            case Pattern::spiral:
            default: return (p.y + bounds.size() / 2) / spacing - (onAxis ? from : from + std::remainder(std::atan2(p.z, p.x) - from, whole)) / whole;
        }
    };
    // Found on the faces cut finely (a few times to a spacing), so the
    // lines are as curved across a big face as the pattern is: each piece
    // then put back on the face it was cut from.
    std::vector<Vec3> fine;
    std::vector<Face> fineFaces;
    std::vector<std::uint32_t> parents;
    std::vector<std::array<Weights, 3>> placings;
    std::vector<std::array<double, 3>> values;
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        const auto& f = faces[index];
        const auto a = corners[f[0]], b = corners[f[1]], c = corners[f[2]];
        const auto longest = std::max({(b - a).length(), (c - b).length(), (a - c).length()});
        const auto cuts = std::clamp(static_cast<int>(std::ceil(longest / (spacing / 6))), 1, 32);
        const auto centre = (a + b + c) / 3.0 - middle;
        const auto from = std::atan2(centre.z, centre.x);
        // Its points by weights, row by row from the first corner.
        const auto first = static_cast<std::uint32_t>(fine.size());
        std::vector<Weights> weightsOf;
        for (int row = 0; row <= cuts; ++row) {
            for (int column = 0; column <= cuts - row; ++column) {
                const Weights weights {static_cast<double>(cuts - row - column) / cuts, static_cast<double>(column) / cuts, static_cast<double>(row) / cuts};
                weightsOf.push_back(weights);
                fine.push_back(a * weights[0] + b * weights[1] + c * weights[2]);
            }
        }
        const auto pointAt = [&](int row, int column) { return static_cast<std::uint32_t>(row * (cuts + 1) - row * (row - 1) / 2 + column); };
        const auto add = [&](std::uint32_t p, std::uint32_t q, std::uint32_t r) {
            fineFaces.push_back({first + p, first + q, first + r});
            parents.push_back(index);
            placings.push_back({weightsOf[p], weightsOf[q], weightsOf[r]});
            values.push_back({valueAt(fine[first + p], from), valueAt(fine[first + q], from), valueAt(fine[first + r], from)});
        };
        for (int row = 0; row < cuts; ++row) {
            for (int column = 0; column < cuts - row; ++column) {
                add(pointAt(row, column), pointAt(row, column + 1), pointAt(row + 1, column));
                if (column + 1 < cuts - row) { add(pointAt(row, column + 1), pointAt(row + 1, column + 1), pointAt(row + 1, column)); }
            }
        }
    }
    auto pieces = detail::contours(fineFaces, detail::byPlace(fine, fineFaces), values);
    const auto onParent = [&](std::uint32_t face, const Weights& weights) {
        Weights placed {};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            for (std::size_t parent = 0; parent < 3; ++parent) { placed[parent] += weights[corner] * placings[face][corner][parent]; }
        }
        return placed;
    };
    for (auto& piece : pieces) { piece = {parents[piece.face], onParent(piece.face, piece.from), onParent(piece.face, piece.to)}; }
    return pieces;
}

// A surface's normals: each face's own, and at each of its corners the
// normal shared with its smooth neighbours there (faces meeting it at less
// than `crease`, radians), so a curved surface turns smoothly across its
// faces and a sharp edge stays sharp.
struct Normals {
    std::vector<Vec3> faces;
    std::vector<std::array<Vec3, 3>> corners;

    // At a point of a face, between its corners'.
    Vec3 at(std::uint32_t face, const Weights& weights) const {
        const auto& c = corners[face];
        return (c[0] * weights[0] + c[1] * weights[1] + c[2] * weights[2]).normalized(1.0e-300).value_or(faces[face]);
    }
};
// Which faces each face's corners are shared with smoothly (by place: a
// mesh may repeat a corner): worked out once for a mesh, so its normals are
// found quickly wherever its corners move after (turned, not bent much).
// A corner's normal is its smooth neighbours' faces' normals, weighted so a
// surface's normals hardly depend on how it is cut into faces.
struct Smoothing {
    // For each face's corner in turn, its smooth neighbours (the face
    // itself among them, and which of each one's corners is at the place)
    // from `start` to the next corner's start.
    std::vector<std::uint32_t> start;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> neighbours;

    static Smoothing of(const std::vector<Vec3>& corners, const std::vector<Face>& faces, double crease = .7) {
        const auto own = faceNormals(corners, faces);
        std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::vector<std::pair<std::uint32_t, std::uint8_t>>> around;
        for (std::uint32_t index = 0; index < faces.size(); ++index) {
            const auto a = detail::placeOf(corners[faces[index][0]]), b = detail::placeOf(corners[faces[index][1]]), c = detail::placeOf(corners[faces[index][2]]);
            if (a == b || b == c || c == a) { continue; }
            for (std::uint8_t corner = 0; corner < 3; ++corner) { around[detail::placeOf(corners[faces[index][corner]])].emplace_back(index, corner); }
        }
        const auto cosCrease = std::cos(crease);
        Smoothing smoothing;
        smoothing.start.reserve(faces.size() * 3 + 1);
        for (std::uint32_t index = 0; index < faces.size(); ++index) {
            for (std::uint8_t corner = 0; corner < 3; ++corner) {
                smoothing.start.push_back(static_cast<std::uint32_t>(smoothing.neighbours.size()));
                // A face with no area has only itself.
                const auto found = around.find(detail::placeOf(corners[faces[index][corner]]));
                if (found == around.end()) {
                    smoothing.neighbours.emplace_back(index, corner);
                    continue;
                }
                for (const auto& neighbour : found->second) {
                    if (own[neighbour.first].dot(own[index]) >= cosCrease) { smoothing.neighbours.push_back(neighbour); }
                }
            }
        }
        smoothing.start.push_back(static_cast<std::uint32_t>(smoothing.neighbours.size()));
        return smoothing;
    }

    // The mesh's normals with its corners at `corners`.
    Normals normals(const std::vector<Vec3>& corners, const std::vector<Face>& faces) const {
        Normals found;
        found.faces = faceNormals(corners, faces);
        found.corners.resize(faces.size());
        // Max's weight: the sine of the face's angle there over its sides'
        // lengths, which is exact for corners on a ball.
        const auto weightAt = [&](std::uint32_t face, std::size_t corner) {
            const auto& f = faces[face];
            const auto& at = corners[f[corner]];
            const auto one = corners[f[(corner + 1) % 3]] - at, other = corners[f[(corner + 2) % 3]] - at;
            const auto lengths = one.dot(one) * other.dot(other);
            return lengths > 1.0e-300 ? one.cross(other).length() / lengths : 0.0;
        };
        for (std::size_t index = 0; index < faces.size(); ++index) {
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto at = index * 3 + corner;
                Vec3 sum;
                for (auto neighbour = start[at]; neighbour < start[at + 1]; ++neighbour) {
                    const auto& [face, itsCorner] = neighbours[neighbour];
                    sum = sum + found.faces[face] * weightAt(face, itsCorner);
                }
                found.corners[index][corner] = sum.normalized(1.0e-300).value_or(found.faces[index]);
            }
        }
        return found;
    }

private:
    static std::vector<Vec3> faceNormals(const std::vector<Vec3>& corners, const std::vector<Face>& faces) {
        std::vector<Vec3> normals(faces.size());
        for (std::size_t index = 0; index < faces.size(); ++index) {
            const auto& f = faces[index];
            normals[index] = (corners[f[1]] - corners[f[0]]).cross(corners[f[2]] - corners[f[0]]).normalized(1.0e-300).value_or(Vec3 {});
        }
        return normals;
    }
};
inline Normals normalsOf(const std::vector<Vec3>& corners, const std::vector<Face>& faces, double crease = .7) { return Smoothing::of(corners, faces, crease).normals(corners, faces); }

// A point of a face on the smooth surface its corners' normals describe,
// rather than the flat face: lifted off the face along the normal there, as
// far as the planes its corners' normals stand on would pull it, halfway
// (Phong tessellation at half, along the normal: on a ball, round to the
// second order, and its normal still pointing from the middle).
inline Vec3 smoothAt(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const Normals& normals, std::uint32_t face, const Weights& weights) {
    const auto& f = faces[face];
    const auto flat = at(corners, f, weights);
    Vec3 onPlanes;
    for (std::size_t corner = 0; corner < 3; ++corner) {
        const auto& normal = normals.corners[face][corner];
        onPlanes = onPlanes + (flat - normal * (flat - corners[f[corner]]).dot(normal)) * weights[corner];
    }
    const auto normal = normals.at(face, weights);
    return flat + normal * ((onPlanes - flat).dot(normal) * .5);
}

// How many straight stretches follow a piece over the smooth surface: one
// for each turn of the normal by `turn` (radians) along it, at most 8.
inline int stretchesOf(const Normals& normals, const Piece& piece, double turn = .1) {
    const auto cosine = std::clamp(normals.at(piece.face, piece.from).dot(normals.at(piece.face, piece.to)), -1.0, 1.0);
    return std::clamp(static_cast<int>(std::ceil(std::acos(cosine) / turn)), 1, 8);
}

// Where faces turn from the eye: within a smooth stretch of surface, where
// the corners' normals (see Normals) turn edge on to it, so a curved
// surface's outline is smooth; at a sharper edge between a face toward the
// eye and one away, along that edge; and along every edge of the surface
// only one face has (a flat drawing's outline).
inline std::vector<Piece> outline(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const Normals& smooth, Vec3 eye, double crease = .7) {
    const auto& normals = smooth.faces;
    const auto keyOf = detail::placeOf;
    const auto cosCrease = std::cos(crease);
    std::vector<std::array<double, 3>> facing(faces.size());
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            facing[index][corner] = smooth.corners[index][corner].dot((eye - corners[faces[index][corner]]).normalized(1.0e-300).value_or(Vec3 {}));
        }
    }
    // How far along a face's side, from one corner to another, the smooth
    // surface (see smoothAt) turns edge on to the eye: between where the
    // corners' facings say, found by halving.
    const auto edgeOn = [&](std::uint32_t face, std::size_t from, std::size_t to, double facingFrom) {
        double low = 0, high = 1;
        for (int step = 0; step < 24; ++step) {
            const auto middle = (low + high) * .5;
            Weights weights {};
            weights[from] = 1 - middle;
            weights[to] = middle;
            const auto facing = smooth.at(face, weights).dot(eye - smoothAt(corners, faces, smooth, face, weights));
            ((facing < 0) == (facingFrom < 0) ? low : high) = middle;
        }
        return (low + high) * .5;
    };
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
            // Found the same way from the faces either side, from the side's
            // first corner by place.
            const auto flip = p[side] > p[next];
            const auto first = flip ? next : side, second = flip ? side : next;
            const auto share = edgeOn(index, first, second, v[first]);
            Weights weights {};
            weights[first] = 1 - share;
            weights[second] = share;
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
    // Across a face the smooth outline bends: each piece over a curved face
    // is cut into stretches, their ends moved square across it onto where
    // the surface is edge on, as far as the face reaches.
    std::vector<Piece> followed;
    for (const auto& piece : detail::chained(std::move(found))) {
        const auto stretches = stretchesOf(smooth, piece, .1);
        if (stretches == 1) {
            followed.push_back(piece);
            continue;
        }
        const auto facingAt = [&](const Weights& weights) { return smooth.at(piece.face, weights).dot(eye - smoothAt(corners, faces, smooth, piece.face, weights)); };
        // Square across the piece, keeping the weights' sum.
        const Vec3 along {piece.to[0] - piece.from[0], piece.to[1] - piece.from[1], piece.to[2] - piece.from[2]};
        const auto across = along.cross({1, 1, 1});
        auto previous = piece.from;
        for (int stretch = 1; stretch <= stretches; ++stretch) {
            const auto share = static_cast<double>(stretch) / stretches;
            Weights next;
            for (std::size_t corner = 0; corner < 3; ++corner) { next[corner] = piece.from[corner] + (piece.to[corner] - piece.from[corner]) * share; }
            if (stretch < stretches) {
                // How far it may move either way and stay on the face.
                double least = -1.0e300, most = 1.0e300;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    if (std::abs(across[corner]) < 1.0e-300) { continue; }
                    const auto limit = -next[corner] / across[corner];
                    (across[corner] > 0 ? least : most) = across[corner] > 0 ? std::max(least, limit) : std::min(most, limit);
                }
                const auto moved = [&](double by) { return Weights {next[0] + across[0] * by, next[1] + across[1] * by, next[2] + across[2] * by}; };
                const auto low = facingAt(moved(least)), high = facingAt(moved(most));
                if (least < most && (low < 0) != (high < 0)) {
                    double from = least, to = most;
                    for (int step = 0; step < 24; ++step) {
                        const auto middle = (from + to) * .5;
                        ((facingAt(moved(middle)) < 0) == (low < 0) ? from : to) = middle;
                    }
                    next = moved((from + to) * .5);
                }
            }
            followed.push_back({piece.face, previous, next});
            previous = next;
        }
    }
    return followed;
}
inline std::vector<Piece> outline(const std::vector<Vec3>& corners, const std::vector<Face>& faces, Vec3 eye, double crease = .7) {
    return outline(corners, faces, normalsOf(corners, faces, crease), eye, crease);
}
}
