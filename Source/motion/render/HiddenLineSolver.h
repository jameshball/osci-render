#pragma once

#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace motion::hidden {
// A run of a segment's parameter s, from A (0) to B (1).
struct Interval {
    double from, to;
};

// Hidden-line removal by exact, analytic occlusion: no per-sample tests.
//
// Everything is in eye space, the eye at the origin looking along +z. A
// point P is hidden by a triangle when the ray from the eye to P passes
// through the triangle first: P lies inside the triangle's view cone (the
// three planes through the eye and each of its edges) and beyond its plane.
// Along a segment A + s(B - A) each of those four tests is linear in s, so a
// triangle hides one interval of s found with four divisions, exactly where
// the line passes behind it. A grid of the triangles' screen bounds limits
// each segment to the triangles it can pass behind; segments are independent,
// so frames and segments parallelise freely.
class Solver {
public:
    // Nothing nearer the eye than this is drawn or hides anything.
    static constexpr double nearPlane = 0.05;
    // Pieces shorter than this fraction of a segment are dropped.
    static constexpr double shortestPiece = 5.0e-4;

    void clear() {
        triangles.clear();
        built = false;
    }

    // A face that hides what is behind it, clipped to the near plane.
    void addTriangle(Vec3 a, Vec3 b, Vec3 c) {
        if (!a.finite() || !b.finite() || !c.finite()) { return; }
        std::array<Vec3, 4> clipped {};
        std::size_t count = 0;
        const std::array<Vec3, 3> corners {a, b, c};
        for (std::size_t index = 0; index < 3; ++index) {
            const auto& current = corners[index];
            const auto& next = corners[(index + 1) % 3];
            const auto inside = current.z > nearPlane, nextInside = next.z > nearPlane;
            if (inside) { clipped[count++] = current; }
            if (inside != nextInside) {
                const auto t = (nearPlane - current.z) / (next.z - current.z);
                clipped[count++] = current + (next - current) * t;
            }
        }
        if (count >= 3) { addClipped(clipped[0], clipped[1], clipped[2]); }
        if (count == 4) { addClipped(clipped[0], clipped[2], clipped[3]); }
        built = false;
    }

    std::size_t triangleCount() const { return triangles.size(); }

    // The visible parts of the segment A to B, as sorted, disjoint intervals of
    // s appended to `out`. Parts nearer than the near plane are not visible.
    void visible(Vec3 a, Vec3 b, std::vector<Interval>& out) {
        if (!a.finite() || !b.finite()) { return; }
        if (!built) { build(); }
        const auto direction = b - a;
        // The part in front of the near plane.
        auto first = 0.0, last = 1.0;
        if (std::abs(direction.z) < 1.0e-300) {
            if (a.z <= nearPlane) { return; }
        } else {
            const auto crossing = (nearPlane - a.z) / direction.z;
            if (direction.z > 0) { first = std::max(first, crossing); } else { last = std::min(last, crossing); }
        }
        if (!(last - first > shortestPiece)) { return; }
        const auto start = a + direction * first, end = a + direction * last;
        const auto farthest = std::max(start.z, end.z);
        const auto u0 = start.x / start.z, v0 = start.y / start.z, u1 = end.x / end.z, v1 = end.y / end.z;
        const Bounds reach {std::min(u0, u1), std::max(u0, u1), std::min(v0, v1), std::max(v0, v1)};
        const auto scale = std::max({a.length(), b.length(), 1.0e-9});
        hidden.clear();
        ++stamp;
        if (stamp == 0) { std::fill(stamps.begin(), stamps.end(), 0u); stamp = 1; }
        bool covered = false;
        forEachCell(u0, v0, u1, v1, [&](std::size_t cell) {
            // Nearest first: the rest of the cell is wholly behind the segment
            // once one is, and a covering face is met early.
            for (auto item = cellStart[cell]; item < cellStart[cell + 1] && !covered; ++item) {
                const auto index = cellItems[item];
                if (nearest[index] > farthest) { break; }
                if (stamps[index] == stamp) { continue; }
                stamps[index] = stamp;
                if (!bounds[index].overlaps(reach)) { continue; }
                const auto& triangle = triangles[index];
                auto low = first, high = last;
                const auto apply = [&](double base, double slope, double tolerance) {
                    if (std::abs(slope) < 1.0e-300) {
                        if (base <= tolerance) { high = low; }
                        return;
                    }
                    const auto root = (tolerance - base) / slope;
                    if (slope > 0) { low = std::max(low, root); } else { high = std::min(high, root); }
                };
                // The cone includes its boundary, so a line behind the edge two
                // faces share is hidden by them together.
                for (std::size_t edge = 0; edge < 3 && high > low; ++edge) {
                    apply(triangle.edges[edge].dot(a), triangle.edges[edge].dot(direction), -triangle.edgeTolerance[edge] * scale);
                }
                if (high > low) { apply(triangle.normal.dot(a) - triangle.offset, triangle.normal.dot(direction), triangle.planeTolerance * scale); }
                if (high - low > shortestPiece) {
                    hidden.push_back({low, high});
                    covered = (low <= first && high >= last) || (hidden.size() > 1 && coveredBy(hidden, first, last));
                }
            }
            return !covered;
        });
        if (covered) { return; }
        std::sort(hidden.begin(), hidden.end(), [](const auto& x, const auto& y) { return x.from < y.from; });
        auto cursor = first;
        for (const auto& piece : hidden) {
            if (piece.from - cursor > shortestPiece) { out.push_back({cursor, piece.from}); }
            cursor = std::max(cursor, piece.to);
        }
        if (last - cursor > shortestPiece) { out.push_back({cursor, last}); }
    }

private:
    struct Bounds {
        double left, right, bottom, top; // on screen
        bool overlaps(const Bounds& other) const { return left <= other.right && other.left <= right && bottom <= other.top && other.bottom <= top; }
    };
    struct Triangle {
        std::array<Vec3, 3> edges; // view cone planes, positive inside
        std::array<double, 3> edgeTolerance;
        Vec3 normal; // positive beyond the face, away from the eye
        double offset, planeTolerance;
        double nearest;
        Bounds screen;
    };

    void addClipped(Vec3 a, Vec3 b, Vec3 c) {
        auto normal = (b - a).cross(c - a);
        const auto area = normal.length();
        auto offset = normal.dot(a);
        // Degenerate, or seen edge-on: nothing behind it is hidden.
        if (!(area > 1.0e-14) || !(std::abs(offset) > 1.0e-12 * area * std::max({a.length(), b.length(), c.length()}))) { return; }
        if (offset < 0) {
            normal = normal * -1.0;
            offset = -offset;
        }
        Triangle triangle;
        triangle.normal = normal;
        triangle.offset = offset;
        // A line on the face itself (its own edges, a coplanar neighbour's)
        // is not hidden by it: it must be this far behind, relative to its
        // distance from the eye.
        triangle.planeTolerance = area * 1.0e-5;
        const std::array<Vec3, 3> corners {a, b, c};
        for (std::size_t edge = 0; edge < 3; ++edge) {
            auto plane = corners[edge].cross(corners[(edge + 1) % 3]);
            if (plane.dot(corners[(edge + 2) % 3]) < 0) { plane = plane * -1.0; }
            triangle.edges[edge] = plane;
            triangle.edgeTolerance[edge] = plane.length() * 1.0e-9;
        }
        triangle.nearest = std::min({a.z, b.z, c.z});
        auto& screen = triangle.screen;
        screen.left = screen.bottom = std::numeric_limits<double>::infinity();
        screen.right = screen.top = -std::numeric_limits<double>::infinity();
        for (const auto& corner : corners) {
            const auto u = corner.x / corner.z, v = corner.y / corner.z;
            screen.left = std::min(screen.left, u);
            screen.right = std::max(screen.right, u);
            screen.bottom = std::min(screen.bottom, v);
            screen.top = std::max(screen.top, v);
        }
        triangles.push_back(triangle);
    }

    // Whether the hidden parts so far cover first..last (a few at most, so a
    // sort is cheap next to what an early finish saves).
    static bool coveredBy(std::vector<Interval>& parts, double first, double last) {
        std::sort(parts.begin(), parts.end(), [](const auto& x, const auto& y) { return x.from < y.from; });
        auto reach = first;
        for (const auto& part : parts) {
            if (part.from - reach > shortestPiece) { return false; }
            reach = std::max(reach, part.to);
            if (last - reach <= shortestPiece) { return true; }
        }
        return false;
    }

    // Bins triangles by their screen bounds into a grid of about one cell per
    // triangle.
    void build() {
        built = true;
        stamps.assign(triangles.size(), 0u);
        stamp = 0;
        left = bottom = std::numeric_limits<double>::infinity();
        double right = -left, top = -bottom;
        for (const auto& triangle : triangles) {
            left = std::min(left, triangle.screen.left);
            right = std::max(right, triangle.screen.right);
            bottom = std::min(bottom, triangle.screen.bottom);
            top = std::max(top, triangle.screen.top);
        }
        columns = rows = triangles.empty() ? 1 : std::clamp(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(triangles.size())))), 1, 256);
        if (triangles.empty() || !(right > left) || !(top > bottom)) {
            left = bottom = 0;
            cellWidth = cellHeight = 1;
            columns = rows = 1;
        } else {
            cellWidth = (right - left) / columns;
            cellHeight = (top - bottom) / rows;
        }
        cellStart.assign(static_cast<std::size_t>(columns * rows) + 1, 0u);
        const auto span = [&](const Triangle& triangle, auto&& visit) {
            const auto c0 = columnOf(triangle.screen.left), c1 = columnOf(triangle.screen.right);
            const auto r0 = rowOf(triangle.screen.bottom), r1 = rowOf(triangle.screen.top);
            for (auto row = r0; row <= r1; ++row) {
                for (auto column = c0; column <= c1; ++column) { visit(static_cast<std::size_t>(row * columns + column)); }
            }
        };
        for (const auto& triangle : triangles) { span(triangle, [&](std::size_t cell) { ++cellStart[cell + 1]; }); }
        for (std::size_t cell = 1; cell < cellStart.size(); ++cell) { cellStart[cell] += cellStart[cell - 1]; }
        cellItems.assign(cellStart.back(), 0u);
        // Nearest first, so each triangle's index orders the cells too.
        std::vector<std::uint32_t> order(triangles.size());
        for (std::uint32_t index = 0; index < order.size(); ++index) { order[index] = index; }
        std::sort(order.begin(), order.end(), [&](auto x, auto y) { return triangles[x].nearest < triangles[y].nearest; });
        std::vector<Triangle> sorted;
        sorted.reserve(triangles.size());
        for (const auto index : order) { sorted.push_back(triangles[index]); }
        triangles = std::move(sorted);
        nearest.resize(triangles.size());
        bounds.resize(triangles.size());
        for (std::size_t index = 0; index < triangles.size(); ++index) {
            nearest[index] = triangles[index].nearest;
            bounds[index] = triangles[index].screen;
        }
        auto fill = cellStart;
        for (std::uint32_t index = 0; index < triangles.size(); ++index) {
            span(triangles[index], [&](std::size_t cell) { cellItems[fill[cell]++] = index; });
        }
    }

    int columnOf(double u) const { return std::clamp(static_cast<int>(std::floor((u - left) / cellWidth)), 0, columns - 1); }
    int rowOf(double v) const { return std::clamp(static_cast<int>(std::floor((v - bottom) / cellHeight)), 0, rows - 1); }

    // Every grid cell the screen line from (u0, v0) to (u1, v1) passes
    // through, in order, until `visit` returns false.
    template <typename Visit>
    void forEachCell(double u0, double v0, double u1, double v1, Visit&& visit) const {
        if (triangles.empty()) { return; }
        // Keep the part inside the grid.
        const auto gridRight = left + cellWidth * columns, gridTop = bottom + cellHeight * rows;
        auto t0 = 0.0, t1 = 1.0;
        const auto du = u1 - u0, dv = v1 - v0;
        const auto clipTo = [&](double p, double q) {
            if (std::abs(p) < 1.0e-300) { return q >= 0; }
            const auto r = q / p;
            if (p < 0) { t0 = std::max(t0, r); } else { t1 = std::min(t1, r); }
            return t0 <= t1;
        };
        if (!clipTo(-du, u0 - left) || !clipTo(du, gridRight - u0) || !clipTo(-dv, v0 - bottom) || !clipTo(dv, gridTop - v0)) { return; }
        const auto su = u0 + du * t0, sv = v0 + dv * t0, eu = u0 + du * t1, ev = v0 + dv * t1;
        auto column = columnOf(su), row = rowOf(sv);
        const auto endColumn = columnOf(eu), endRow = rowOf(ev);
        const auto stepColumn = eu > su ? 1 : -1, stepRow = ev > sv ? 1 : -1;
        const auto ru = eu - su, rv = ev - sv;
        const auto infinity = std::numeric_limits<double>::infinity();
        // Distances along the line (0..1) to the next cell boundaries.
        auto nextU = std::abs(ru) < 1.0e-300 ? infinity : ((left + (column + (stepColumn > 0 ? 1 : 0)) * cellWidth) - su) / ru;
        auto nextV = std::abs(rv) < 1.0e-300 ? infinity : ((bottom + (row + (stepRow > 0 ? 1 : 0)) * cellHeight) - sv) / rv;
        const auto deltaU = std::abs(ru) < 1.0e-300 ? infinity : cellWidth / std::abs(ru);
        const auto deltaV = std::abs(rv) < 1.0e-300 ? infinity : cellHeight / std::abs(rv);
        for (int guard = 0; guard < columns + rows + 2; ++guard) {
            if (!visit(static_cast<std::size_t>(row * columns + column))) { return; }
            if (column == endColumn && row == endRow) { return; }
            if (nextU < nextV) {
                column += stepColumn;
                nextU += deltaU;
            } else {
                row += stepRow;
                nextV += deltaV;
            }
            if (column < 0 || column >= columns || row < 0 || row >= rows) { return; }
        }
    }

    std::vector<Triangle> triangles;
    // What every query reads first, kept apart and compact.
    std::vector<double> nearest;
    std::vector<Bounds> bounds;
    bool built = false;
    int columns = 1, rows = 1;
    double left = 0, bottom = 0, cellWidth = 1, cellHeight = 1;
    std::vector<std::uint32_t> cellStart, cellItems, stamps;
    std::uint32_t stamp = 0;
    std::vector<Interval> hidden;
};
}
