#pragma once

#include "TrianglePoints.h"
#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <numbers>
#include <optional>
#include <tuple>
#include <vector>

// Lines drawn on a surface: its outline as an eye sees it (where it turns
// from the eye) and patterns over it that show its shape: a spiral round it,
// slices through it, rings round its middle, contours of noise. Each piece
// lies on one face, between two points given by their weights on its
// corners, so whoever draws it can put it wherever the face's corners go
// (the source's own space, or the scene). Pieces come in order along each
// line, end to end. Pure geometry, so it can be tested alone.
namespace motion::hidden::surface {
using Face = std::array<std::uint32_t, 3>;

// A stretch of line across one face.
struct Piece {
    std::uint32_t face;
    Weights from, to;
};

// The point at `weights` on a face.
inline Vec3 at(const std::vector<Vec3>& corners, const Face& face, const Weights& weights) { return weighted(corners[face[0]], corners[face[1]], corners[face[2]], weights); }

enum class Pattern { spiral, noise, slices, rings };

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
using Place = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
inline Place placeOf(Vec3 p) { return {std::llround(p.x * 1.0e7), std::llround(p.y * 1.0e7), std::llround(p.z * 1.0e7)}; }

// Places and meetings looked up by hash: a fine grid has a great many.
struct TripleHash {
    template <typename A, typename B, typename C>
    std::size_t operator()(const std::tuple<A, B, C>& triple) const {
        auto hash = static_cast<std::uint64_t>(std::get<0>(triple)) * 0x9e3779b97f4a7c15ull;
        hash = (hash ^ static_cast<std::uint64_t>(std::get<1>(triple))) * 0xbf58476d1ce4e5b9ull;
        hash = (hash ^ static_cast<std::uint64_t>(std::get<2>(triple))) * 0x94d049bb133111ebull;
        return static_cast<std::size_t>(hash ^ (hash >> 31));
    }
};

// The faces with each corner as the first corner at its place: a mesh may
// repeat a place (a ball's poles, a seam), and lines meet there all the same.
inline std::vector<Face> byPlace(const std::vector<Vec3>& corners, const std::vector<Face>& faces) {
    std::unordered_map<Place, std::uint32_t, TripleHash> first;
    first.reserve(corners.size());
    std::vector<Face> placed(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            placed[index][corner] = first.try_emplace(placeOf(corners[faces[index][corner]]), faces[index][corner]).first->second;
        }
    }
    return placed;
}

// Each side of the faces with area, by its ends' places (the lower first),
// with the faces it is a side of and which side of each it is.
inline std::map<std::pair<Place, Place>, std::vector<std::pair<std::uint32_t, std::size_t>>> sidesOf(const std::vector<Vec3>& corners, const std::vector<Face>& faces) {
    std::map<std::pair<Place, Place>, std::vector<std::pair<std::uint32_t, std::size_t>>> sides;
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        // A face with no area (two corners at one place) has no sides.
        const std::array places {placeOf(corners[faces[index][0]]), placeOf(corners[faces[index][1]]), placeOf(corners[faces[index][2]])};
        if (places[0] == places[1] || places[1] == places[2] || places[2] == places[0]) { continue; }
        for (std::size_t side = 0; side < 3; ++side) {
            auto a = places[side], b = places[(side + 1) % 3];
            if (b < a) { std::swap(a, b); }
            sides[{a, b}].push_back({index, side});
        }
    }
    return sides;
}

// Pieces put in order along their lines: each one's neighbour is the one
// meeting a side where it does. Loops go round once; lines go end to end.
inline std::vector<Piece> chained(std::vector<Found> found) {
    std::unordered_map<Meeting, std::vector<std::size_t>, TripleHash> at;
    at.reserve(found.size() * 2);
    for (std::size_t index = 0; index < found.size(); ++index) {
        for (const auto& end : found[index].ends) {
            if (end.has_value()) { at[*end].push_back(index); }
        }
    }
    std::vector<char> used(found.size(), 0);
    // The other piece meeting `end`'s place, and which of its ends that is:
    // one not yet drawn, where more than two meet (a saddle), so a line
    // goes on where it can.
    const auto across = [&](std::size_t index, std::size_t end) -> std::optional<std::pair<std::size_t, std::size_t>> {
        const auto& meeting = found[index].ends[end];
        if (!meeting.has_value()) { return std::nullopt; }
        std::optional<std::pair<std::size_t, std::size_t>> drawn;
        for (const auto other : at[*meeting]) {
            if (other == index) { continue; }
            const std::pair next {other, found[other].ends[0] == meeting ? std::size_t {0} : std::size_t {1}};
            if (used[other] == 0) { return next; }
            if (!drawn.has_value()) { drawn = next; }
        }
        return drawn;
    };
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
        // Faces are cut finer than the lines are apart, so each crosses a
        // few levels: more is a pattern run away, drawn as nothing.
        constexpr double mostLevels = 256;
        const auto least = std::min({v[0], v[1], v[2]}), most = std::max({v[0], v[1], v[2]});
        if (!std::isfinite(least) || !std::isfinite(most) || most - least > mostLevels || std::abs(least) > 1.0e15) { continue; }
        const auto low = static_cast<std::int64_t>(std::ceil(least - nudge));
        const auto high = static_cast<std::int64_t>(std::floor(most - nudge));
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

// How far up a surface each point of it is, from its lowest part, over it
// (see pattern's spiral): found over its faces each cut evenly into a grid,
// `cuts` across, the same along every side so neighbours' grids meet, and
// read off the grid inside a face. It climbs outward from the lowest
// points only, so round a tube (a torus lying down) it climbs up the
// outside, over the top and down the inside, all the way round, not up both
// sides to meet at the top: `round` is then how far it is round, where it
// comes back to its start (zero for a surface with nothing round it). A
// surface flat across its height (`flat`) is climbed in from its rim (its
// edges no other face shares), or from its outermost points if it has none.
// Each part of it (faces joined to no others) is climbed from its own.
struct Climb {
    int cuts = 1;
    std::vector<std::vector<std::uint32_t>> grids;
    std::vector<double> up;
    double highest = 0, round = 0;
    // Each part's bounds, and the part each face is of (none for a face
    // with no area).
    static constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
    std::vector<Bounds> parts;
    std::vector<std::uint32_t> faceParts;

    static Climb over(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const Bounds& bounds, int cuts, bool flat) {
        Climb climb;
        climb.cuts = cuts;
        // Edges of one face only, by their ends' places.
        std::map<std::pair<Place, Place>, int> edgeUses;
        const auto edgeOf = [](Place a, Place b) { return a < b ? std::pair {a, b} : std::pair {b, a}; };
        if (flat) {
            for (const auto& f : faces) {
                for (std::size_t corner = 0; corner < 3; ++corner) { ++edgeUses[edgeOf(placeOf(corners[f[corner]]), placeOf(corners[f[(corner + 1) % 3]]))]; }
            }
        }
        std::vector<char> rim;
        std::vector<Vec3> points;
        std::map<Place, std::uint32_t> at;
        std::vector<std::vector<std::pair<std::uint32_t, double>>> next;
        const auto pointAt = [&](int row, int column) { return static_cast<std::size_t>(gridIndex(cuts, row, column)); };
        const auto join = [&](std::uint32_t a, std::uint32_t b) {
            if (a == b) { return; }
            const auto length = (points[b] - points[a]).length();
            next[a].emplace_back(b, length);
            next[b].emplace_back(a, length);
        };
        climb.grids.resize(faces.size());
        for (std::size_t index = 0; index < faces.size(); ++index) {
            const auto& f = faces[index];
            const auto a = corners[f[0]], b = corners[f[1]], c = corners[f[2]];
            const auto pa = placeOf(a), pb = placeOf(b), pc = placeOf(c);
            if (pa == pb || pb == pc || pc == pa) { continue; }
            auto& grid = climb.grids[index];
            for (int row = 0; row <= cuts; ++row) {
                for (int column = 0; column <= cuts - row; ++column) {
                    const auto point = a * (static_cast<double>(cuts - row - column) / cuts) + b * (static_cast<double>(column) / cuts) + c * (static_cast<double>(row) / cuts);
                    const auto [found, added] = at.try_emplace(placeOf(point), static_cast<std::uint32_t>(points.size()));
                    if (added) {
                        points.push_back(point);
                        next.emplace_back();
                        rim.push_back(0);
                    }
                    grid.push_back(found->second);
                }
            }
            if (flat) {
                // Its grid's points along each of its sides on the rim.
                const auto onRim = [&](Place from, Place to) { return edgeUses[edgeOf(from, to)] == 1; };
                for (int step = 0; step <= cuts; ++step) {
                    if (onRim(pa, pb)) { rim[grid[pointAt(0, step)]] = 1; }
                    if (onRim(pa, pc)) { rim[grid[pointAt(step, 0)]] = 1; }
                    if (onRim(pb, pc)) { rim[grid[pointAt(step, cuts - step)]] = 1; }
                }
            }
            for (int row = 0; row < cuts; ++row) {
                for (int column = 0; column < cuts - row; ++column) {
                    const auto p = grid[pointAt(row, column)], q = grid[pointAt(row, column + 1)], r = grid[pointAt(row + 1, column)];
                    join(p, q);
                    join(q, r);
                    join(r, p);
                }
            }
        }
        // The parts, each point's, and what each holds.
        std::vector<std::uint32_t> partOf(points.size(), none);
        std::vector<std::uint32_t> reached;
        for (std::uint32_t seed = 0; seed < points.size(); ++seed) {
            if (partOf[seed] != none) { continue; }
            const auto part = static_cast<std::uint32_t>(climb.parts.size());
            auto& box = climb.parts.emplace_back(Bounds {points[seed], points[seed]});
            partOf[seed] = part;
            reached.assign(1, seed);
            while (!reached.empty()) {
                const auto point = reached.back();
                reached.pop_back();
                const auto& p = points[point];
                box.low = {std::min(box.low.x, p.x), std::min(box.low.y, p.y), std::min(box.low.z, p.z)};
                box.high = {std::max(box.high.x, p.x), std::max(box.high.y, p.y), std::max(box.high.z, p.z)};
                for (const auto& [other, length] : next[point]) {
                    if (partOf[other] != none) { continue; }
                    partOf[other] = part;
                    reached.push_back(other);
                }
            }
        }
        climb.faceParts.assign(faces.size(), none);
        for (std::size_t index = 0; index < faces.size(); ++index) {
            if (!climb.grids[index].empty()) { climb.faceParts[index] = partOf[climb.grids[index].front()]; }
        }
        const auto outFrom = [&](std::uint32_t point) {
            const auto middle = climb.parts[partOf[point]].middle();
            return std::hypot(points[point].x - middle.x, points[point].z - middle.z);
        };
        std::vector<double> outermost(climb.parts.size(), 0.0);
        std::vector<char> hasRim(climb.parts.size(), 0);
        for (std::uint32_t point = 0; point < points.size(); ++point) {
            outermost[partOf[point]] = std::max(outermost[partOf[point]], outFrom(point));
            hasRim[partOf[point]] |= rim[point];
        }
        const auto lowest = [&](std::uint32_t point) {
            const auto part = partOf[point];
            if (!flat) { return points[point].y <= climb.parts[part].low.y + 1.0e-4 * bounds.size(); }
            return hasRim[part] != 0 ? rim[point] != 0 : outFrom(point) >= outermost[part] - 1.0e-4 * bounds.size();
        };
        // Whether a side may be climbed: not from the lowest points more
        // inward than up (under a tube, not up a cone). Across a flat
        // surface every way is inward.
        bool restricted = !flat;
        const auto inward = [&](std::uint32_t from, std::uint32_t to) {
            return restricted && lowest(from) && !lowest(to) && outFrom(from) - outFrom(to) > points[to].y - points[from].y;
        };
        auto& up = climb.up;
        using Reached = std::pair<double, std::uint32_t>;
        std::vector<Reached> queue;
        const auto climbFromLowest = [&] {
            up.assign(points.size(), 1.0e300);
            queue.clear();
            for (std::uint32_t point = 0; point < points.size(); ++point) {
                if (!lowest(point)) { continue; }
                up[point] = 0;
                queue.emplace_back(0.0, point);
            }
            std::make_heap(queue.begin(), queue.end(), std::greater<>());
            while (!queue.empty()) {
                std::pop_heap(queue.begin(), queue.end(), std::greater<>());
                const auto [distance, point] = queue.back();
                queue.pop_back();
                if (distance > up[point]) { continue; }
                for (const auto& [other, length] : next[point]) {
                    if (inward(point, other) || !(distance + length < up[other])) { continue; }
                    up[other] = distance + length;
                    queue.emplace_back(up[other], other);
                    std::push_heap(queue.begin(), queue.end(), std::greater<>());
                }
            }
        };
        climbFromLowest();
        // What can't be climbed to that way (nothing round it) is climbed to
        // any way.
        if (std::any_of(up.begin(), up.end(), [](double value) { return value > 1.0e299; })) {
            restricted = false;
            climbFromLowest();
        }
        for (auto& value : up) {
            if (value > 1.0e299) { value = 0; }
            climb.highest = std::max(climb.highest, value);
        }
        // All the way round: back to the lowest points from inside.
        std::vector<double> back;
        for (std::uint32_t point = 0; point < points.size(); ++point) {
            for (const auto& [other, length] : next[point]) {
                if (inward(point, other)) { back.push_back(up[other] + length); }
            }
        }
        if (!back.empty()) {
            std::sort(back.begin(), back.end());
            climb.round = back[back.size() / 2];
            climb.highest = climb.round;
        }
        return climb;
    }

    // At a point of a face, by its weights. A grid cell across where it
    // comes back round has its start a whole way round on.
    double at(std::uint32_t face, const Weights& weights) const {
        const auto& grid = grids[face];
        if (grid.empty()) { return 0; }
        const auto x = std::clamp(weights[1], 0.0, 1.0) * cuts, y = std::clamp(weights[2], 0.0, 1.0) * cuts;
        const auto row = std::clamp(static_cast<int>(std::floor(y)), 0, cuts - 1);
        const auto column = std::clamp(static_cast<int>(std::floor(x)), 0, cuts - 1 - row);
        const auto fx = x - column, fy = y - row;
        const auto pointAt = [&](int r, int c) { return up[grid[static_cast<std::size_t>(gridIndex(cuts, r, c))]]; };
        std::array<double, 3> ups, shares;
        if (fx + fy <= 1 || row + column + 2 > cuts) {
            ups = {pointAt(row, column), pointAt(row, column + 1), pointAt(row + 1, column)};
            shares = {1 - fx - fy, fx, fy};
        } else {
            ups = {pointAt(row, column + 1), pointAt(row + 1, column), pointAt(row + 1, column + 1)};
            shares = {1 - fy, 1 - fx, fx + fy - 1};
        }
        if (round > 0 && *std::max_element(ups.begin(), ups.end()) - *std::min_element(ups.begin(), ups.end()) > round / 2) {
            for (auto& value : ups) { value += value < round / 2 ? round : 0.0; }
        }
        return ups[0] * shares[0] + ups[1] * shares[1] + ups[2] * shares[2];
    }
};

// Lines whose ends lie within `reach` of each other joined end to end (each
// end to its nearest), the joining piece's end moved onto where the line
// before it ends: faces next to each other cut the side they share at
// different points, so a line crossing it can stop just short of where it
// goes on. Pieces whose ends are within `same` follow on already.
inline std::vector<Piece> mended(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const std::vector<Piece>& pieces, double reach, double same) {
    const auto start = [&](const Piece& piece) { return at(corners, faces[piece.face], piece.from); };
    const auto end = [&](const Piece& piece) { return at(corners, faces[piece.face], piece.to); };
    // A point's weights on a piece's face, as it lies on the face's plane.
    const auto weightsOnFace = [&](const Piece& piece, Vec3 point) {
        const auto& face = faces[piece.face];
        return weightsOn(corners[face[0]], corners[face[1]], corners[face[2]], point);
    };
    // The lines, as runs of the pieces, and where each starts and ends.
    struct Line { std::size_t first, last; };
    std::vector<Line> lines;
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        if (index == 0 || (start(pieces[index]) - end(pieces[index - 1])).length() >= same) { lines.push_back({index, index}); }
        lines.back().last = index;
    }
    const auto endOf = [&](std::size_t line, int side) { return side == 0 ? start(pieces[lines[line].first]) : end(pieces[lines[line].last]); };
    // Each open line's ends paired with others', nearest first; a line's own
    // two ends closing it into a loop.
    struct Pairing { double distance; std::size_t line; int side; std::size_t other; int otherSide; };
    std::vector<Pairing> pairings;
    std::vector<char> open(lines.size(), 0);
    for (std::size_t line = 0; line < lines.size(); ++line) { open[line] = (endOf(line, 0) - endOf(line, 1)).length() >= same ? 1 : 0; }
    for (std::size_t line = 0; line < lines.size(); ++line) {
        if (open[line] == 0) { continue; }
        for (std::size_t other = line; other < lines.size(); ++other) {
            if (open[other] == 0) { continue; }
            for (int side = 0; side < 2; ++side) {
                for (int otherSide = other == line ? 1 - side : 0; otherSide < 2; ++otherSide) {
                    if (other == line && side == 1) { continue; }
                    const auto distance = (endOf(line, side) - endOf(other, otherSide)).length();
                    if (distance < reach) { pairings.push_back({distance, line, side, other, otherSide}); }
                }
            }
        }
    }
    if (pairings.empty()) { return pieces; }
    std::sort(pairings.begin(), pairings.end(), [](const Pairing& a, const Pairing& b) { return a.distance < b.distance; });
    // What each end is joined to: a line and its end, or none.
    constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
    std::vector<std::array<std::pair<std::size_t, int>, 2>> joined(lines.size(), {std::pair {none, 0}, std::pair {none, 0}});
    for (const auto& pairing : pairings) {
        if (joined[pairing.line][static_cast<std::size_t>(pairing.side)].first != none || joined[pairing.other][static_cast<std::size_t>(pairing.otherSide)].first != none) { continue; }
        joined[pairing.line][static_cast<std::size_t>(pairing.side)] = {pairing.other, pairing.otherSide};
        joined[pairing.other][static_cast<std::size_t>(pairing.otherSide)] = {pairing.line, pairing.side};
    }
    // Walked from an end joined to nothing (or anywhere round a loop), each
    // line from the end it is entered by.
    std::vector<Piece> result;
    result.reserve(pieces.size());
    std::vector<char> walked(lines.size(), 0);
    const auto walk = [&](std::size_t line, int side) {
        const auto first = result.size();
        while (line != none && walked[line] == 0) {
            walked[line] = 1;
            const auto& run = lines[line];
            const auto joining = result.size();
            if (side == 0) {
                result.insert(result.end(), pieces.begin() + static_cast<std::ptrdiff_t>(run.first), pieces.begin() + static_cast<std::ptrdiff_t>(run.last) + 1);
            } else {
                for (auto index = run.last + 1; index-- > run.first;) { result.push_back({pieces[index].face, pieces[index].to, pieces[index].from}); }
            }
            if (joining > first) {
                auto& piece = result[joining];
                piece.from = weightsOnFace(piece, end(result[joining - 1]));
            }
            const auto next = joined[line][static_cast<std::size_t>(1 - side)];
            line = next.first;
            side = next.second;
        }
        // Back round to where it began: a loop, closed exactly.
        if (line != none && result.size() > first) {
            auto& piece = result.back();
            piece.to = weightsOnFace(piece, start(result[first]));
        }
    };
    for (std::size_t line = 0; line < lines.size(); ++line) {
        if (walked[line] != 0) { continue; }
        if (joined[line][0].first == none) {
            walk(line, 0);
        } else if (joined[line][1].first == none) {
            walk(line, 1);
        }
    }
    for (std::size_t line = 0; line < lines.size(); ++line) {
        if (walked[line] == 0) { walk(line, 0); }
    }
    return result;
}

// Pieces following on over one face joined into one while every point they
// pass stays within `tolerance` of the line from the first's start to the
// last's end. Pieces whose ends are within `same` follow on.
inline std::vector<Piece> straightened(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const std::vector<Piece>& pieces, double tolerance, double same) {
    const auto flat = [&](const Piece& piece, const Weights& weights) { return at(corners, faces[piece.face], weights); };
    std::vector<Piece> joined;
    joined.reserve(pieces.size());
    std::size_t start = 0;
    while (start < pieces.size()) {
        auto end = start;
        const auto from = flat(pieces[start], pieces[start].from);
        // Each next piece on the same face, starting where the last ended,
        // taken while every point passed stays near the line to its end.
        while (end + 1 < pieces.size() && pieces[end + 1].face == pieces[start].face && (flat(pieces[end + 1], pieces[end + 1].from) - flat(pieces[end], pieces[end].to)).length() < same) {
            const auto to = flat(pieces[end + 1], pieces[end + 1].to);
            const auto line = to - from;
            const auto length = line.length();
            bool straight = true;
            for (auto passed = start; passed <= end && straight; ++passed) {
                const auto point = flat(pieces[passed], pieces[passed].to) - from;
                const auto off = length > 0 ? point.cross(line).length() / length : point.length();
                straight = off < tolerance;
            }
            if (!straight) { break; }
            ++end;
        }
        joined.push_back({pieces[start].face, pieces[start].from, pieces[end].to});
        start = end + 1;
    }
    return joined;
}

// The lines, less every closed loop shorter than `shortest`.
inline std::vector<Piece> withoutBeads(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const std::vector<Piece>& pieces, double shortest, double same) {
    const auto flat = [&](const Piece& piece, const Weights& weights) { return at(corners, faces[piece.face], weights); };
    const auto followsOn = [same](Vec3 end, Vec3 start) { return (start - end).length() < same; };
    std::vector<Piece> kept;
    kept.reserve(pieces.size());
    std::size_t first = 0;
    while (first < pieces.size()) {
        auto last = first;
        double length = (flat(pieces[first], pieces[first].to) - flat(pieces[first], pieces[first].from)).length();
        while (last + 1 < pieces.size() && followsOn(flat(pieces[last], pieces[last].to), flat(pieces[last + 1], pieces[last + 1].from))) {
            ++last;
            length += (flat(pieces[last], pieces[last].to) - flat(pieces[last], pieces[last].from)).length();
        }
        const auto closed = followsOn(flat(pieces[last], pieces[last].to), flat(pieces[first], pieces[first].from));
        if (!closed || length >= shortest) { kept.insert(kept.end(), pieces.begin() + static_cast<std::ptrdiff_t>(first), pieces.begin() + static_cast<std::ptrdiff_t>(last) + 1); }
        first = last + 1;
    }
    return kept;
}
}

// The most points a pattern cuts a surface's faces into to find its lines,
// as many as the spiral's climb (the example projects use a fifth of it).
inline constexpr double mostPatternPoints = 3.0e5;

// A pattern over the faces, `across` lines across the surface's largest
// side, or for slices and the spiral, up its height. A spiral winds about
// the surface's own up (y) through its middle, slices are level across it,
// and rings centre on its front (its nearest along z).
// `turn`: how far round its loop noise has morphed (a whole turn brings it
// back as it was).
inline std::vector<Piece> pattern(const std::vector<Vec3>& corners, const std::vector<Face>& faces, Pattern kind, double across, double turn = 0) {
    if (faces.empty() || !(across > 0)) { return {}; }
    // Faces with a corner that isn't finite (a mesh an effect broke) are
    // left out; the pieces on the rest keep their faces' numbers.
    const auto finite = [&corners](const Face& face) { return std::all_of(face.begin(), face.end(), [&corners](std::uint32_t corner) { return corners[corner].finite(); }); };
    if (!std::all_of(faces.begin(), faces.end(), finite)) {
        std::vector<Face> kept;
        std::vector<std::uint32_t> original;
        for (std::uint32_t index = 0; index < faces.size(); ++index) {
            if (!finite(faces[index])) { continue; }
            kept.push_back(faces[index]);
            original.push_back(index);
        }
        auto pieces = pattern(corners, kept, kind, across, turn);
        for (auto& piece : pieces) { piece.face = original[piece.face]; }
        return pieces;
    }
    const auto bounds = detail::boundsOf(corners, faces);
    // A surface flat across x or z (a card facing the eye) spirals about the
    // axis through it, as if lying down: its axes swapped to make that its
    // height. The pieces are by face and weights, so are the same either way.
    const auto thin = bounds.size() * 1.0e-3;
    const auto extent = bounds.high - bounds.low;
    if (kind == Pattern::spiral && extent.y > thin && (extent.x <= thin || extent.z <= thin)) {
        std::vector<Vec3> lying;
        lying.reserve(corners.size());
        for (const auto& corner : corners) { lying.push_back(extent.x <= thin ? Vec3 {corner.y, corner.x, corner.z} : Vec3 {corner.x, corner.z, corner.y}); }
        return pattern(lying, faces, kind, across, turn);
    }
    // Flat across its height: a spiral winds in from its rim.
    const auto level = !(extent.y > thin);
    const auto spacing = bounds.size() / across;
    // Slices and the spiral are spaced by its height, so a flat thing (a
    // torus lying down) has as many across its thickness as a tall one up it.
    const auto height = std::max(bounds.high.y - bounds.low.y, bounds.size() * 1.0e-3);
    const auto along = height / across;
    // The spiral climbs by how far up the surface a point is from its lowest
    // part, over it (see Climb): first roughly, by its corners, to know how
    // far apart its lines are, then over its faces cut as finely as that,
    // so a big flat face (a box's top) is climbed across, and the spiral
    // winds in across it rather than star-like out from its middle.
    std::optional<detail::Climb> climb;
    double highest = 0;
    if (kind == Pattern::spiral) {
        const auto rough = detail::Climb::over(corners, faces, bounds, 1, level);
        double longest = 0;
        for (const auto& f : faces) { longest = std::max({longest, (corners[f[1]] - corners[f[0]]).length(), (corners[f[2]] - corners[f[1]]).length(), (corners[f[0]] - corners[f[2]]).length()}); }
        const auto apart = std::min(spacing, std::max(rough.highest, bounds.size() * 1.0e-3) / across);
        // Cut to a third of the lines' spacing, within reason: a grid point
        // per corner of a fine mesh already, a few hundred thousand at most.
        auto cuts = std::clamp(static_cast<int>(std::ceil(longest / (apart / 3))), 1, 64);
        while (cuts > 1 && static_cast<double>(faces.size()) * (cuts + 1) * (cuts + 2) / 2 > 3.0e5) { cuts = (cuts + 1) / 2 == cuts ? cuts - 1 : (cuts + 1) / 2; }
        climb = cuts == 1 ? rough : detail::Climb::over(corners, faces, bounds, cuts, level);
        highest = climb->highest;
    }
    const auto climbing = std::max(highest, bounds.size() * 1.0e-3) / across;
    // Its lines climb steeply enough to be seen to (at about 18 degrees),
    // like a screw's thread: as many side by side as keep them `climbing`
    // apart, each a turn further up for every turn round. Across a flat
    // surface it is one line, a record's groove.
    // Each part winds about its own middle.
    const auto startsRound = [&](const detail::Bounds& part) {
        const auto around = std::max(part.high.x - part.low.x, part.high.z - part.low.z) / 2;
        return level ? 1.0 : std::max(1.0, std::round(2 * std::numbers::pi * around * std::tan(18 * std::numbers::pi / 180) / climbing));
    };
    std::vector<double> partStarts;
    if (climb.has_value()) {
        for (const auto& part : climb->parts) { partStarts.push_back(startsRound(part)); }
    }
    const auto middle = bounds.middle();
    // The part of the face being found, as the spiral winds round it.
    auto axis = middle;
    auto starts = startsRound(bounds);
    constexpr double whole = 2 * std::numbers::pi;
    // Noise morphs as three fields mixed by a point going round a circle of
    // the sphere of their mixes, tipped off its middle: as strong all the way
    // round, and never at its opposite (which would draw the same lines), so
    // it comes back only after a whole turn.
    constexpr double tip = .5;
    const std::array<double, 3> mix {std::cos(tip) * std::cos(whole * turn), std::cos(tip) * std::sin(whole * turn), std::sin(tip)};
    // The pattern's value at a point: its lines are where it is whole. A
    // spiral's angle is unwound from `from`, so it doesn't jump a turn across
    // a face, and on its axis, where it has none, is `from`.
    const auto valueAt = [&](Vec3 point, double from, double up) {
        const auto p = point - middle;
        const auto round = point - axis;
        const auto onAxis = std::hypot(round.x, round.z) < 1.0e-9 * bounds.size();
        switch (kind) {
            // Half a spacing in from the ends, so none is lost at a tip.
            case Pattern::slices: return (p.y + middle.y - bounds.low.y) / along + .5;
            // Round its front (its middle, at its nearest along z): a bullseye seen from in front.
            case Pattern::rings: return (p - Vec3 {0, 0, bounds.high.z - middle.z}).length() / spacing + .5;
            case Pattern::noise: {
                const auto at = p * (across / bounds.size() * .5);
                return 2.5 * (mix[0] * detail::noise(at + Vec3 {17.3, 5.1, 9.7}) + mix[1] * detail::noise(at + Vec3 {-41.7, 23.9, 3.3}) + mix[2] * detail::noise(at + Vec3 {8.1, -29.3, 61.7}));
            }
            case Pattern::spiral:
            default: return up / climbing + .5 - starts * (onAxis ? from : from + std::remainder(std::atan2(round.z, round.x) - from, whole)) / whole;
        }
    };
    // As finely as its lines are spaced (by climb for a spiral); slices,
    // level, are straight across a flat face already.
    const auto apart = kind == Pattern::spiral ? std::min(spacing, climbing) : spacing;
    std::vector<int> wanted(faces.size(), 1);
    if (kind != Pattern::slices) {
        for (std::size_t index = 0; index < faces.size(); ++index) {
            const auto& f = faces[index];
            const auto a = corners[f[0]], b = corners[f[1]], c = corners[f[2]];
            const auto longest = std::max({(b - a).length(), (c - b).length(), (a - c).length()});
            wanted[index] = std::clamp(static_cast<int>(std::ceil(longest / (apart / 6))), 1, 32);
        }
    }
    // Within a budget of points over the whole surface: many big faces
    // (filled text, a fanned polygon) are cut less finely, rather than each
    // into a thousand triangles.
    const auto pointsCut = [&](int most) {
        double total = 0;
        for (const auto want : wanted) {
            const auto cuts = std::min(want, most);
            total += (cuts + 1) * (cuts + 2) / 2;
        }
        return total;
    };
    auto most = 32;
    while (most > 1 && pointsCut(most) > mostPatternPoints) { --most; }
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
        const auto cuts = std::min(wanted[index], most);
        const auto part = climb.has_value() ? climb->faceParts[index] : detail::Climb::none;
        axis = part != detail::Climb::none ? climb->parts[part].middle() : middle;
        starts = part != detail::Climb::none ? partStarts[part] : startsRound(bounds);
        const auto centre = (a + b + c) / 3.0 - axis;
        const auto from = std::atan2(centre.z, centre.x);
        // Its points by weights, row by row from the first corner.
        const auto first = static_cast<std::uint32_t>(fine.size());
        std::vector<Weights> weightsOf;
        // How far up the surface each of its points is.
        const auto upAt = [&](const Weights& weights) { return climb.has_value() ? climb->at(index, weights) : 0.0; };
        // The pattern's value at each, found once: the triangles about a
        // point share it.
        std::vector<double> valueOf;
        for (int row = 0; row <= cuts; ++row) {
            for (int column = 0; column <= cuts - row; ++column) {
                const Weights weights {static_cast<double>(cuts - row - column) / cuts, static_cast<double>(column) / cuts, static_cast<double>(row) / cuts};
                weightsOf.push_back(weights);
                fine.push_back(a * weights[0] + b * weights[1] + c * weights[2]);
                valueOf.push_back(valueAt(fine.back(), from, upAt(weights)));
            }
        }
        const auto pointAt = [&](int row, int column) { return static_cast<std::uint32_t>(gridIndex(cuts, row, column)); };
        const auto add = [&](std::uint32_t p, std::uint32_t q, std::uint32_t r) {
            fineFaces.push_back({first + p, first + q, first + r});
            parents.push_back(index);
            placings.push_back({weightsOf[p], weightsOf[q], weightsOf[r]});
            values.push_back({valueOf[p], valueOf[q], valueOf[r]});
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
    // How far apart its lines are: those ending nearer each other than an
    // eighth of that are one line, broken where faces met.
    const auto lineSpacing = kind == Pattern::spiral ? std::min(spacing, climbing) : kind == Pattern::slices ? std::min(spacing, along) : spacing;
    // Within this, a piece starts where the last ends: it follows on.
    const auto same = 1.0e-9 * bounds.size() + 1.0e-12;
    pieces = detail::mended(corners, faces, pieces, lineSpacing / 8, same);
    // Then pieces that follow on over one face joined while they stay near
    // straight (within a 250th of the spacing: looser visibly flattens
    // curves), so a line is as few pieces as draw it well: the beam has only
    // so many points a cycle.
    pieces = detail::straightened(corners, faces, pieces, lineSpacing * .004, same);
    // A loop shorter than half a spacing (noise's, about a peak or a dip,
    // as it grows from nothing while the noise flows) draws as a bright
    // bead, the beam lingering on its corners: it is left out until it is
    // big enough to draw as a line.
    return detail::withoutBeads(corners, faces, pieces, spacing / 2, same);
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
    // Each face's corner takes its normal from a group: its smooth
    // neighbours (the face itself among them, and which of each one's
    // corners is at the place), from `start` to the next group's start.
    // The faces at a place facing almost the same way (a fanned polygon's
    // many triangles about its first corner) share a group, so a place is
    // summed once for each way its faces face, not once for each face.
    std::vector<std::uint32_t> groupOf;
    std::vector<std::size_t> start;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> neighbours;

    // Past this many faces at a place that can't be grouped by how they
    // face (more ways than mostWays, or near the crease from each other),
    // each is left its own flat normal rather than compared with every other.
    static constexpr std::size_t mostCompared = 1024, mostWays = 32;

    static Smoothing of(const std::vector<Vec3>& corners, const std::vector<Face>& faces, double crease = .7) {
        using Corner = std::pair<std::uint32_t, std::uint8_t>;
        const auto own = faceNormals(corners, faces);
        std::map<detail::Place, std::vector<Corner>> around;
        // Corners of faces with no area: not neighbours, but they have some.
        std::vector<Corner> flat;
        for (std::uint32_t index = 0; index < faces.size(); ++index) {
            const auto a = detail::placeOf(corners[faces[index][0]]), b = detail::placeOf(corners[faces[index][1]]), c = detail::placeOf(corners[faces[index][2]]);
            if (a == b || b == c || c == a) {
                for (std::uint8_t corner = 0; corner < 3; ++corner) { flat.emplace_back(index, corner); }
                continue;
            }
            for (std::uint8_t corner = 0; corner < 3; ++corner) { around[detail::placeOf(corners[faces[index][corner]])].emplace_back(index, corner); }
        }
        const auto cosCrease = std::cos(crease);
        // Faces within `closeAngle` (radians) of a way's first face face that way.
        // Two ways' faces are then all smooth, or all not, unless the ways
        // are within twice that (and rounding) of the crease apart.
        const auto closeAngle = crease * 1.0e-3;
        const auto cosNear = std::cos(closeAngle);
        const auto grouping = crease > 1.0e-3 && crease < 3;
        Smoothing smoothing;
        smoothing.groupOf.assign(faces.size() * 3, 0);
        // Group 0 is empty: its corners keep their face's own normal.
        smoothing.start = {0, 0};
        const auto next = [&smoothing]() { return static_cast<std::uint32_t>(smoothing.start.size() - 1); };
        const auto close = [&smoothing]() { smoothing.start.push_back(smoothing.neighbours.size()); };
        // A group of the faces at a place smooth with a face facing `normal`.
        const auto compared = [&](const std::vector<Corner>& here, Vec3 normal) -> std::uint32_t {
            if (here.size() > mostCompared) { return 0; }
            const auto group = next();
            for (const auto& neighbour : here) {
                if (own[neighbour.first].dot(normal) >= cosCrease) { smoothing.neighbours.push_back(neighbour); }
            }
            close();
            return group;
        };
        const auto plain = [](Vec3 normal) { return normal.finite() && normal.dot(normal) > .5; };
        struct Way {
            Vec3 normal;
            std::uint32_t group = 0;
            bool mixed = false;
        };
        std::vector<Way> ways;
        std::vector<std::size_t> wayOf;
        const auto wayFacing = [&](Vec3 normal) {
            for (std::size_t way = 0; way < ways.size(); ++way) {
                if (normal.dot(ways[way].normal) >= cosNear) { return way; }
            }
            return ways.size();
        };
        // Each place's ways, kept for the flat faces' corners there.
        std::map<detail::Place, std::vector<Way>> placeWays;
        for (const auto& [place, here] : around) {
            ways.clear();
            wayOf.assign(here.size(), std::numeric_limits<std::size_t>::max());
            for (std::size_t member = 0; grouping && member < here.size() && ways.size() <= mostWays; ++member) {
                const auto& normal = own[here[member].first];
                if (!plain(normal)) { continue; }
                wayOf[member] = wayFacing(normal);
                if (wayOf[member] == ways.size()) { ways.push_back({normal}); }
            }
            if (!grouping || ways.size() > mostWays) {
                for (const auto& [face, corner] : here) { smoothing.groupOf[static_cast<std::size_t>(face) * 3 + corner] = compared(here, own[face]); }
                continue;
            }
            // Whether each way's faces are all smooth with each other way's.
            std::vector<std::uint8_t> smooth(ways.size() * ways.size());
            for (std::size_t one = 0; one < ways.size(); ++one) {
                for (std::size_t other = 0; other < ways.size(); ++other) {
                    const auto apart = std::acos(std::clamp(ways[one].normal.dot(ways[other].normal), -1.0, 1.0));
                    const auto slack = 2 * near + 1.0e-7;
                    if (apart + slack < crease) {
                        smooth[one * ways.size() + other] = 1;
                    } else if (apart - slack <= crease) {
                        ways[one].mixed = true;
                    }
                }
            }
            for (std::size_t way = 0; way < ways.size(); ++way) {
                if (ways[way].mixed) { continue; }
                ways[way].group = next();
                for (std::size_t member = 0; member < here.size(); ++member) {
                    // A face with no plain normal (no area) is smooth with all or none alike.
                    const auto with = wayOf[member] < ways.size() ? smooth[way * ways.size() + wayOf[member]] != 0 : own[here[member].first].dot(ways[way].normal) >= cosCrease;
                    if (with) { smoothing.neighbours.push_back(here[member]); }
                }
                close();
            }
            for (std::size_t member = 0; member < here.size(); ++member) {
                const auto [face, corner] = here[member];
                const auto way = wayOf[member];
                smoothing.groupOf[static_cast<std::size_t>(face) * 3 + corner] = way < ways.size() && !ways[way].mixed ? ways[way].group : compared(here, own[face]);
            }
            if (!flat.empty()) { placeWays.emplace(place, ways); }
        }
        for (const auto& [face, corner] : flat) {
            const auto place = detail::placeOf(corners[faces[face][corner]]);
            const auto at = static_cast<std::size_t>(face) * 3 + corner;
            const auto found = around.find(place);
            if (found == around.end()) {
                smoothing.groupOf[at] = next();
                smoothing.neighbours.emplace_back(face, corner);
                close();
                continue;
            }
            const auto known = placeWays.find(place);
            if (known != placeWays.end() && plain(own[face])) {
                ways = known->second;
                const auto way = wayFacing(own[face]);
                if (way < ways.size() && !ways[way].mixed) {
                    smoothing.groupOf[at] = ways[way].group;
                    continue;
                }
            }
            smoothing.groupOf[at] = compared(found->second, own[face]);
        }
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
        std::vector<Vec3> sums(start.size() - 1);
        for (std::size_t group = 0; group + 1 < start.size(); ++group) {
            Vec3 sum;
            for (auto neighbour = start[group]; neighbour < start[group + 1]; ++neighbour) {
                const auto& [face, itsCorner] = neighbours[neighbour];
                sum = sum + found.faces[face] * weightAt(face, itsCorner);
            }
            sums[group] = sum;
        }
        for (std::size_t index = 0; index < faces.size(); ++index) {
            for (std::size_t corner = 0; corner < 3; ++corner) {
                found.corners[index][corner] = sums[groupOf[index * 3 + corner]].normalized(1.0e-300).value_or(found.faces[index]);
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

// Which faces meet where, by their corners' places: found once for a mesh
// whose corners only move after (turned, not torn), for its outline each
// frame.
struct Topology {
    std::vector<Face> placed;
    std::map<std::pair<detail::Place, detail::Place>, std::vector<std::pair<std::uint32_t, std::size_t>>> sides;
    static Topology of(const std::vector<Vec3>& corners, const std::vector<Face>& faces) { return {detail::byPlace(corners, faces), detail::sidesOf(corners, faces)}; }
};

// Where faces turn from the eye: within a smooth stretch of surface, where
// the corners' normals (see Normals) turn edge on to it, so a curved
// surface's outline is smooth; at a sharper edge between a face toward the
// eye and one away, along that edge; and along every edge of the surface
// only one face has (a flat drawing's outline).
inline std::vector<Piece> outline(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const Normals& smooth, const Topology& topology, Vec3 eye, double crease = .7) {
    const auto& normals = smooth.faces;
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
    const auto& placed = topology.placed;
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
    for (const auto& [key, sharing] : topology.sides) {
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
            auto next = mixed(piece.from, piece.to, share);
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
}
