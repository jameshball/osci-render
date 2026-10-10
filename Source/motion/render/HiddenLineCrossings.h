#pragma once

#include "HiddenLineDepth.h"
#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

// Where surfaces that hide cut through each other. An arm sunk into a body
// shows where it comes out: a line neither drawing has, but an edge of what is
// seen all the same. Pure geometry in eye space, so it can be tested alone.
namespace motion::hidden::crossing {
// A triangle of a surface that hides, in eye space.
struct Triangle {
    std::array<Vec3, 3> eye;
    std::uint32_t surface;
};

// A point on a triangle, as the weights of its corners.
using Weights = std::array<double, 3>;

// One piece of a crossing, on the triangle (of the earlier surface) it
// belongs to.
struct Piece {
    std::uint32_t triangle;
    Weights from, to;
    Vec3 eyeFrom, eyeTo;
};

// Where a triangle passes through a plane, from its corners' signed distances
// to it: the two points where its edges cross. None when it stays on one
// side or only touches (a corner or an edge on the plane, or lying in it):
// surfaces that only touch hide nothing of each other, and their edges are
// drawn already.
inline std::optional<std::array<Weights, 2>> through(const std::array<double, 3>& distance, double tolerance) {
    std::array<int, 3> side {};
    for (std::size_t corner = 0; corner < 3; ++corner) { side[corner] = distance[corner] > tolerance ? 1 : distance[corner] < -tolerance ? -1 : 0; }
    const auto on = static_cast<int>(side[0] == 0) + static_cast<int>(side[1] == 0) + static_cast<int>(side[2] == 0);
    const auto edgePoint = [&](std::size_t from, std::size_t to) {
        Weights weights {};
        const auto t = distance[from] / (distance[from] - distance[to]);
        weights[from] = 1 - t;
        weights[to] = t;
        return weights;
    };
    if (on == 0) {
        // One corner alone on its side: the crossing joins its two edges.
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto next = (corner + 1) % 3, last = (corner + 2) % 3;
            if (side[corner] != side[next] && side[corner] != side[last]) { return std::array<Weights, 2> {edgePoint(corner, next), edgePoint(corner, last)}; }
        }
        return std::nullopt;
    }
    if (on == 1) {
        // Through a corner and across the far edge, or only touching there.
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (side[corner] != 0) { continue; }
            const auto next = (corner + 1) % 3, last = (corner + 2) % 3;
            if (side[next] == side[last]) { return std::nullopt; }
            Weights at {};
            at[corner] = 1;
            return std::array<Weights, 2> {at, edgePoint(next, last)};
        }
    }
    return std::nullopt;
}

inline Vec3 at(const std::array<Vec3, 3>& corners, const Weights& weights) { return corners[0] * weights[0] + corners[1] * weights[1] + corners[2] * weights[2]; }

// Where triangles `a` and `b` (indices into `triangles`) cut through each
// other, if they do.
inline std::optional<Piece> between(const std::vector<Triangle>& triangles, std::uint32_t a, std::uint32_t b) {
    const auto& first = triangles[a];
    const auto& second = triangles[b];
    const auto unitA = (first.eye[1] - first.eye[0]).cross(first.eye[2] - first.eye[0]).normalized(1.0e-300);
    const auto unitB = (second.eye[1] - second.eye[0]).cross(second.eye[2] - second.eye[0]).normalized(1.0e-300);
    if (!unitA.has_value() || !unitB.has_value()) { return std::nullopt; }
    // Distances count as zero within a millionth of the triangles' size.
    const auto size = std::max({(first.eye[1] - first.eye[0]).length(), (first.eye[2] - first.eye[0]).length(), (second.eye[1] - second.eye[0]).length(), (second.eye[2] - second.eye[0]).length()});
    const auto tolerance = size * 1.0e-6;
    std::array<double, 3> toA {}, toB {};
    for (std::size_t corner = 0; corner < 3; ++corner) {
        toA[corner] = unitA->dot(second.eye[corner] - first.eye[0]);
        toB[corner] = unitB->dot(first.eye[corner] - second.eye[0]);
    }
    const auto cutA = through(toB, tolerance);
    if (!cutA.has_value()) { return std::nullopt; }
    const auto cutB = through(toA, tolerance);
    if (!cutB.has_value()) { return std::nullopt; }
    // Both cuts lie on the line where the planes meet: where they overlap
    // along it, the triangles cross.
    const auto direction = unitA->cross(*unitB);
    const auto along = [&](const Triangle& triangle, const std::array<Weights, 2>& cut) { return std::array<double, 2> {direction.dot(at(triangle.eye, cut[0])), direction.dot(at(triangle.eye, cut[1]))}; };
    const auto alongA = along(first, *cutA), alongB = along(second, *cutB);
    const auto low = std::max(std::min(alongA[0], alongA[1]), std::min(alongB[0], alongB[1]));
    const auto high = std::min(std::max(alongA[0], alongA[1]), std::max(alongB[0], alongB[1]));
    if (!(high - low > tolerance * direction.length())) { return std::nullopt; }
    // The overlap's ends, on the triangle the crossing belongs to.
    const auto ownedByA = first.surface < second.surface;
    const auto& owner = ownedByA ? first : second;
    const auto& cut = ownedByA ? *cutA : *cutB;
    const auto& range = ownedByA ? alongA : alongB;
    const auto weightsAt = [&](double position) {
        const auto t = range[1] != range[0] ? (position - range[0]) / (range[1] - range[0]) : 0.0;
        Weights weights {};
        for (std::size_t corner = 0; corner < 3; ++corner) { weights[corner] = cut[0][corner] + (cut[1][corner] - cut[0][corner]) * t; }
        return weights;
    };
    const auto from = weightsAt(low), to = weightsAt(high);
    return Piece {ownedByA ? a : b, from, to, at(owner.eye, from), at(owner.eye, to)};
}

namespace detail {
// Pieces end to end as runs: a piece follows another of the same surface
// that ends where it starts (turned round if need be). Ends meet within a
// millionth of a unit, found through a hash of the cells they fall in.
inline std::vector<std::vector<Piece>> joined(std::vector<Piece>& pieces, const std::vector<Triangle>& triangles) {
    constexpr double reach = 1.0e-6;
    using Cell = std::array<std::int64_t, 3>;
    const auto cellOf = [](const Vec3& point) { return Cell {static_cast<std::int64_t>(std::floor(point.x / reach)), static_cast<std::int64_t>(std::floor(point.y / reach)), static_cast<std::int64_t>(std::floor(point.z / reach))}; };
    const auto hash = [](const Cell& cell) { return static_cast<std::size_t>((cell[0] * 73856093) ^ (cell[1] * 19349663) ^ (cell[2] * 83492791)); };
    std::unordered_multimap<std::size_t, std::size_t> ends;
    ends.reserve(pieces.size() * 2);
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        ends.emplace(hash(cellOf(pieces[index].eyeFrom)), index);
        ends.emplace(hash(cellOf(pieces[index].eyeTo)), index);
    }
    const auto meets = [](const Vec3& p, const Vec3& q) { return (p - q).length() <= reach; };
    std::vector<bool> used(pieces.size(), false);
    // An unused piece of `surface` with an end at `point`.
    const auto nextAt = [&](const Vec3& point, std::uint32_t surface) -> std::optional<std::size_t> {
        const auto centre = cellOf(point);
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                for (std::int64_t dz = -1; dz <= 1; ++dz) {
                    const auto range = ends.equal_range(hash({centre[0] + dx, centre[1] + dy, centre[2] + dz}));
                    for (auto it = range.first; it != range.second; ++it) {
                        const auto& candidate = pieces[it->second];
                        if (!used[it->second] && triangles[candidate.triangle].surface == surface && (meets(candidate.eyeFrom, point) || meets(candidate.eyeTo, point))) { return it->second; }
                    }
                }
            }
        }
        return std::nullopt;
    };
    const auto reversed = [](Piece piece) {
        std::swap(piece.from, piece.to);
        std::swap(piece.eyeFrom, piece.eyeTo);
        return piece;
    };
    std::vector<std::vector<Piece>> runs;
    for (std::size_t start = 0; start < pieces.size(); ++start) {
        if (used[start]) { continue; }
        used[start] = true;
        const auto surface = triangles[pieces[start].triangle].surface;
        std::vector<Piece> forward {pieces[start]}, backward;
        for (auto next = nextAt(forward.back().eyeTo, surface); next.has_value(); next = nextAt(forward.back().eyeTo, surface)) {
            used[*next] = true;
            forward.push_back(meets(pieces[*next].eyeFrom, forward.back().eyeTo) ? pieces[*next] : reversed(pieces[*next]));
        }
        for (auto next = nextAt(pieces[start].eyeFrom, surface); next.has_value(); next = nextAt(backward.back().eyeFrom, surface)) {
            used[*next] = true;
            const auto& head = backward.empty() ? pieces[start] : backward.back();
            backward.push_back(meets(pieces[*next].eyeTo, head.eyeFrom) ? pieces[*next] : reversed(pieces[*next]));
        }
        std::vector<Piece> run(backward.rbegin(), backward.rend());
        run.insert(run.end(), forward.begin(), forward.end());
        runs.push_back(std::move(run));
    }
    return runs;
}
}

// Every crossing between different surfaces' triangles, in runs: pieces end
// to end as the triangles they lie on are, so each run draws as one line.
// Triangles are paired through a grid over the screen; `focalLength`
// projects eye space onto it, and pairs wholly off the frame are never seen.
inline std::vector<std::vector<Piece>> runs(const std::vector<Triangle>& triangles, double focalLength) {
    std::vector<Piece> pieces;
    if (triangles.size() < 2) { return {}; }
    struct Box {
        double left, right, bottom, top, near, far;
    };
    std::vector<Box> boxes;
    boxes.reserve(triangles.size());
    for (const auto& triangle : triangles) {
        Box box {1.0e300, -1.0e300, 1.0e300, -1.0e300, 1.0e300, -1.0e300};
        for (const auto& corner : triangle.eye) {
            const auto x = corner.x / corner.z * focalLength, y = corner.y / corner.z * focalLength;
            box = {std::min(box.left, x), std::max(box.right, x), std::min(box.bottom, y), std::max(box.top, y), std::min(box.near, corner.z), std::max(box.far, corner.z)};
        }
        boxes.push_back(box);
    }
    constexpr int cells = 64;
    constexpr auto extent = DepthBuffer::extent;
    const auto cellOf = [](double value) { return std::clamp(static_cast<int>(std::floor((value + extent) / (2 * extent) * cells)), 0, cells - 1); };
    std::vector<std::vector<std::uint32_t>> grid(static_cast<std::size_t>(cells * cells));
    for (std::uint32_t index = 0; index < triangles.size(); ++index) {
        const auto& box = boxes[index];
        if (box.right < -extent || box.left > extent || box.top < -extent || box.bottom > extent) { continue; }
        for (auto y = cellOf(box.bottom); y <= cellOf(box.top); ++y) {
            for (auto x = cellOf(box.left); x <= cellOf(box.right); ++x) { grid[static_cast<std::size_t>(y * cells + x)].push_back(index); }
        }
    }
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const auto& cell = grid[static_cast<std::size_t>(y * cells + x)];
            for (std::size_t i = 0; i < cell.size(); ++i) {
                for (std::size_t j = i + 1; j < cell.size(); ++j) {
                    if (triangles[cell[i]].surface == triangles[cell[j]].surface) { continue; }
                    const auto& p = boxes[cell[i]];
                    const auto& q = boxes[cell[j]];
                    if (p.right < q.left || q.right < p.left || p.top < q.bottom || q.top < p.bottom || p.far < q.near || q.far < p.near) { continue; }
                    // Each pair once: in the cell where their boxes' overlap starts.
                    if (cellOf(std::max(p.left, q.left)) != x || cellOf(std::max(p.bottom, q.bottom)) != y) { continue; }
                    const auto piece = between(triangles, cell[i], cell[j]);
                    if (piece.has_value()) { pieces.push_back(*piece); }
                }
            }
        }
    }
    return detail::joined(pieces, triangles);
}
}
