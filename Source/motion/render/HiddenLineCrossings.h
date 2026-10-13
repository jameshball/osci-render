#pragma once

#include <atomic>

#include "HiddenLineDepth.h"
#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

// Where surfaces that hide cut through each other. An arm sunk into a body
// shows where it comes out: a line neither drawing has, but an edge of what is
// seen all the same. Pure geometry in eye space, so it can be tested alone.
namespace motion::hidden::crossing {
// A triangle of a surface that hides, in eye space (or, for runsInScene,
// in the scene).
struct Triangle {
    std::array<Vec3, 3> eye;
    std::uint32_t surface;
    // How much its surface mirrors: a crossing is drawn on the surface that
    // shows its own lines more, as a mirror's give way to what it shows.
    float mirrors = 0;
};

// A point on a triangle, as the weights of its corners.
using Weights = std::array<double, 3>;

// One piece of a crossing, on the triangle it belongs to: the one of the
// surface that mirrors less, or of the earlier surface.
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
    const auto ownedByA = first.mirrors != second.mirrors ? first.mirrors < second.mirrors : first.surface < second.surface;
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
// A triangle's reach on screen and in depth.
struct Box {
    double left, right, bottom, top, near, far;
    static Box empty() { return {1.0e300, -1.0e300, 1.0e300, -1.0e300, 1.0e300, -1.0e300}; }
    void include(const Box& other) {
        left = std::min(left, other.left);
        right = std::max(right, other.right);
        bottom = std::min(bottom, other.bottom);
        top = std::max(top, other.top);
        near = std::min(near, other.near);
        far = std::max(far, other.far);
    }
    bool overlaps(const Box& other) const {
        return !(right < other.left || other.right < left || top < other.bottom || other.top < bottom || far < other.near || other.far < near);
    }
};
inline Box boxOf(const Triangle& triangle, double focalLength) {
    auto box = Box::empty();
    for (const auto& corner : triangle.eye) {
        const auto x = corner.x / corner.z * focalLength, y = corner.y / corner.z * focalLength;
        box.include({x, x, y, y, corner.z, corner.z});
    }
    return box;
}

// Triangles bucketed by the cells their boxes cover of an area of the screen,
// in one flat array (counted, then filled), with cells about twice the
// triangles' size.
class ScreenGrid {
public:
    ScreenGrid(const std::vector<std::uint32_t>& members, const std::vector<Box>& boxes, double typicalSize, const Box& area) : area(area) {
        const auto span = std::max(area.right - area.left, area.top - area.bottom);
        cells = std::clamp(static_cast<int>(span / std::max(2 * typicalSize, 1.0e-9)), 16, 512);
        starts.assign(static_cast<std::size_t>(cells * cells) + 1, 0);
        const auto covered = [&](const Box& box, auto&& visit) {
            for (auto y = axisCell(box.bottom, area.bottom, area.top); y <= axisCell(box.top, area.bottom, area.top); ++y) {
                for (auto x = axisCell(box.left, area.left, area.right); x <= axisCell(box.right, area.left, area.right); ++x) { visit(y * cells + x); }
            }
        };
        for (const auto member : members) { covered(boxes[member], [&](int cell) { ++starts[static_cast<std::size_t>(cell) + 1]; }); }
        for (std::size_t index = 1; index < starts.size(); ++index) { starts[index] += starts[index - 1]; }
        entries.resize(starts.back());
        auto fill = starts;
        for (const auto member : members) { covered(boxes[member], [&](int cell) { entries[fill[static_cast<std::size_t>(cell)]++] = member; }); }
    }
    int cellCount() const { return cells * cells; }
    std::span<const std::uint32_t> cell(int index) const { return {entries.data() + starts[static_cast<std::size_t>(index)], starts[static_cast<std::size_t>(index) + 1] - starts[static_cast<std::size_t>(index)]}; }
    int cellAt(double x, double y) const { return axisCell(y, area.bottom, area.top) * cells + axisCell(x, area.left, area.right); }

private:
    int axisCell(double value, double low, double high) const {
        return std::clamp(static_cast<int>(std::floor((value - low) / (high - low) * cells)), 0, cells - 1);
    }
    Box area;
    int cells = 16;
    std::vector<std::size_t> starts;
    std::vector<std::uint32_t> entries;
};

// Pieces end to end as runs: a piece follows another of the same surface
// that ends where it starts (turned round if need be). Ends meet within a
// millionth of a unit, found through a hash of the cells they fall in.
inline std::vector<std::vector<Piece>> joined(std::vector<Piece>& pieces, const std::vector<Triangle>& triangles) {
    constexpr double reach = 1.0e-6;
    using Cell = std::array<std::int64_t, 3>;
    const auto cellOf = [](const Vec3& point) { return Cell {static_cast<std::int64_t>(std::floor(point.x / reach)), static_cast<std::int64_t>(std::floor(point.y / reach)), static_cast<std::int64_t>(std::floor(point.z / reach))}; };
    // Unsigned, so the multiplies wrap rather than overflow.
    const auto hash = [](const Cell& cell) { return static_cast<std::size_t>((static_cast<std::uint64_t>(cell[0]) * 73856093u) ^ (static_cast<std::uint64_t>(cell[1]) * 19349663u) ^ (static_cast<std::uint64_t>(cell[2]) * 83492791u)); };
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
// `triangles` comes surface by surface. Only triangles where another surface
// overlaps them on screen and in depth can cross it; those are paired through
// a grid over the screen sized to them, and only across surfaces, never
// within one. `focalLength` projects eye space onto the screen; nothing past
// what the Scope shows (`reach`) is ever seen.
// None, once `cancel` is set partway (a bake stopping, the app closing).
inline std::vector<std::vector<Piece>> runs(const std::vector<Triangle>& triangles, double focalLength, Reach reach = {}, const std::atomic<bool>* cancel = nullptr) {
    std::vector<Piece> pieces;
    if (triangles.size() < 2) { return {}; }
    using detail::Box;
    std::vector<Box> boxes;
    boxes.reserve(triangles.size());
    std::vector<Box> surfaceBoxes;
    for (const auto& triangle : triangles) {
        auto box = detail::boxOf(triangle, focalLength);
        boxes.push_back(box);
        if (surfaceBoxes.size() <= triangle.surface) { surfaceBoxes.resize(triangle.surface + 1, Box::empty()); }
        surfaceBoxes[triangle.surface].include(box);
    }
    // Where some other surface could be.
    const auto x = reach.x * DepthBuffer::overscan, y = reach.y * DepthBuffer::overscan;
    const auto frame = Box {-x, x, -y, y, -1.0e300, 1.0e300};
    std::vector<std::uint32_t> candidates;
    std::vector<double> extents;
    for (std::uint32_t index = 0; index < triangles.size(); ++index) {
        const auto& box = boxes[index];
        if (!box.overlaps(frame)) { continue; }
        const auto own = triangles[index].surface;
        bool shared = false;
        for (std::uint32_t surface = 0; surface < surfaceBoxes.size() && !shared; ++surface) { shared = surface != own && box.overlaps(surfaceBoxes[surface]); }
        if (!shared) { continue; }
        candidates.push_back(index);
        extents.push_back(std::max(box.right - box.left, box.top - box.bottom));
    }
    if (candidates.size() < 2) { return {}; }
    // Cells sized to the middling triangle: a few huge ones (a floor seen
    // close) would otherwise make every cell hold thousands of small ones.
    const auto middle = extents.begin() + static_cast<std::ptrdiff_t>(extents.size() / 2);
    std::nth_element(extents.begin(), middle, extents.end());
    const detail::ScreenGrid grid(candidates, boxes, *middle, frame);
    for (int cell = 0; cell < grid.cellCount(); ++cell) {
        if ((cell & 63) == 0 && cancel != nullptr && cancel->load(std::memory_order_relaxed)) { return {}; }
        const auto members = grid.cell(cell);
        // Members come surface by surface: each surface's run is paired with
        // every later surface's.
        for (std::size_t first = 0; first < members.size();) {
            auto firstEnd = first;
            while (firstEnd < members.size() && triangles[members[firstEnd]].surface == triangles[members[first]].surface) { ++firstEnd; }
            for (auto i = first; i < firstEnd; ++i) {
                for (auto j = firstEnd; j < members.size(); ++j) {
                    const auto& p = boxes[members[i]];
                    const auto& q = boxes[members[j]];
                    if (!p.overlaps(q)) { continue; }
                    // Each pair once: in the cell where their boxes' overlap starts.
                    if (grid.cellAt(std::max(p.left, q.left), std::max(p.bottom, q.bottom)) != cell) { continue; }
                    const auto piece = between(triangles, members[i], members[j]);
                    if (piece.has_value()) { pieces.push_back(*piece); }
                }
            }
            first = firstEnd;
        }
    }
    return detail::joined(pieces, triangles);
}

// Every crossing between different surfaces' triangles anywhere in the
// scene, not just where an eye sees: what a mirror shows can be anywhere.
// `triangles`' corners are in the scene, surface by surface. Only surfaces
// whose boxes overlap are paired, and only their triangles inside the other's
// box, swept along x. None, once `cancel` is set partway.
inline std::vector<std::vector<Piece>> runsInScene(const std::vector<Triangle>& triangles, const std::atomic<bool>* cancel = nullptr) {
    struct Bounds {
        Vec3 low {1.0e300, 1.0e300, 1.0e300}, high {-1.0e300, -1.0e300, -1.0e300};
        void include(const Bounds& other) {
            low = {std::min(low.x, other.low.x), std::min(low.y, other.low.y), std::min(low.z, other.low.z)};
            high = {std::max(high.x, other.high.x), std::max(high.y, other.high.y), std::max(high.z, other.high.z)};
        }
        bool overlaps(const Bounds& other) const {
            return low.x <= other.high.x && other.low.x <= high.x && low.y <= other.high.y && other.low.y <= high.y && low.z <= other.high.z && other.low.z <= high.z;
        }
    };
    std::vector<Bounds> boxes(triangles.size());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> surfaces;
    std::vector<Bounds> surfaceBoxes;
    for (std::uint32_t index = 0; index < triangles.size(); ++index) {
        for (const auto& corner : triangles[index].eye) { boxes[index].include({corner, corner}); }
        if (surfaces.empty() || triangles[surfaces.back().first].surface != triangles[index].surface) {
            surfaces.push_back({index, index});
            surfaceBoxes.emplace_back();
        }
        surfaces.back().second = index + 1;
        surfaceBoxes.back().include(boxes[index]);
    }
    std::vector<Piece> pieces;
    std::vector<std::uint32_t> mine, theirs;
    for (std::size_t one = 0; one < surfaces.size(); ++one) {
        for (auto other = one + 1; other < surfaces.size(); ++other) {
            if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) { return {}; }
            if (!surfaceBoxes[one].overlaps(surfaceBoxes[other])) { continue; }
            const auto within = [&](std::size_t surface, const Bounds& box, std::vector<std::uint32_t>& found) {
                found.clear();
                for (auto index = surfaces[surface].first; index < surfaces[surface].second; ++index) {
                    if (boxes[index].overlaps(box)) { found.push_back(index); }
                }
                std::sort(found.begin(), found.end(), [&](std::uint32_t a, std::uint32_t b) { return boxes[a].low.x < boxes[b].low.x; });
            };
            within(one, surfaceBoxes[other], mine);
            within(other, surfaceBoxes[one], theirs);
            std::size_t first = 0;
            for (const auto a : mine) {
                while (first < theirs.size() && boxes[theirs[first]].high.x < boxes[a].low.x) { ++first; }
                for (auto index = first; index < theirs.size() && boxes[theirs[index]].low.x <= boxes[a].high.x; ++index) {
                    if (!boxes[a].overlaps(boxes[theirs[index]])) { continue; }
                    const auto piece = between(triangles, a, theirs[index]);
                    if (piece.has_value()) { pieces.push_back(*piece); }
                }
            }
        }
    }
    return detail::joined(pieces, triangles);
}
}
