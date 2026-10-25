#pragma once

#include "HiddenLineDepth.h"
#include "HiddenLineMidpoints.h"
#include "Lighting.h"
#include "TrianglePoints.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace motion::hidden {
// A point of a face being diced: where it is in its source, and in the scene
// once bent.
struct DiceCorner {
    osci::Point source;
    Vec3 at;
};

// A view a frame's surfaces are drawn into: the camera's, or one of a light's.
// Its buffer's pixel, in the units its projection maps the eye's space to.
struct DiceView {
    Eye eye;
    Projection projection;
    Reach reach;
    double pixel = DepthBuffer::pixel;
};

// Cuts a face something bends into pieces flat enough, once bent, to draw as
// triangles in every view at once: the frame's surfaces are bent once, in the
// scene, and each view draws the same pieces. A piece is measured in the
// pixels of the view that sees it most finely (its footprint: the size of a
// pixel there, for the nearest of its corners), so it is as fine as the
// finest view needs and no finer; one no view sees is dropped. Large pieces
// are halved across their longest edge, through a new point bent like any
// other. Once a piece is a few cells across, it is cut into an even grid
// instead, each of whose points is bent once: as fine as a cell where the
// bend curves it most, coarser where it keeps the piece flat. A grid piece
// the bend still stretches is halved on.
class Dicer {
public:
    // Edges, in pixels, past which pieces are halved, and past which a piece
    // is too large to grid.
    static constexpr double cell = 4, gridReach = 8 * cell;
    // How far a piece drawn flat may be from the bent surface, in pixels.
    static constexpr double flatTolerance = .125;

    // How many pieces a frame's surfaces may be cut into, some eight to each
    // cell of the depth buffer. Close to an eye a bend can throw a face
    // across the whole view again with every fold, so halving would go on to
    // its deepest; past this, pieces are drawn as they are.
    static constexpr std::size_t budget = std::size_t {1} << 19;

    // A frame's dicing, for these views (kept until it is done).
    void begin(std::span<const DiceView> seenBy) {
        views = seenBy;
        spent = 0;
    }
    // A surface: shared edges of its faces are bent once.
    void nextSurface() { midpoints.clear(); }

    // `bend` places a source point in the scene; `emit` takes each piece.
    // A face can take many pieces, so `cancelled` is asked every so often;
    // false once it stops the dicing.
    template <typename Bend, typename Emit, typename Cancelled>
    bool dice(const DiceCorner& a, const DiceCorner& b, const DiceCorner& c, Bend&& bend, Emit&& emit, Cancelled&& cancelled) {
        stopped = false;
        std::size_t emitted = 0;
        auto asked = [&](const DiceCorner& p, const DiceCorner& q, const DiceCorner& r) {
            if ((++emitted & 1023) == 0 && cancelled()) { stopped = true; }
            if (!stopped) { emit(p, q, r); }
        };
        halve(a, b, c, deepest, true, cell * cell, std::nullopt, bend, asked);
        return !stopped;
    }
    template <typename Bend, typename Emit>
    void dice(const DiceCorner& a, const DiceCorner& b, const DiceCorner& c, Bend&& bend, Emit&& emit) {
        dice(a, b, c, bend, emit, [] { return false; });
    }

    // The size of a pixel at a piece in the view that sees it most finely,
    // or none when no view sees it.
    std::optional<double> footprint(const Vec3& p, const Vec3& q, const Vec3& r) const {
        auto finest = std::numeric_limits<double>::infinity();
        for (const auto& view : views) {
            const std::array<Vec3, 3> seen {view.eye.see(p), view.eye.see(q), view.eye.see(r)};
            const auto& projection = view.projection;
            // Wholly past one side of the view (or behind its eye) it doesn't see it.
            const auto x = view.reach.x * DepthBuffer::overscan, y = view.reach.y * DepthBuffer::overscan;
            int left = 0, right = 0, below = 0, above = 0, behind = 0;
            auto nearest = std::numeric_limits<double>::infinity();
            for (const auto& point : seen) {
                if (!projection.parallel && !(point.z > nearPlane)) {
                    ++behind;
                    continue;
                }
                const auto scale = projection.parallel ? projection.focalLength : projection.focalLength / point.z;
                const auto sx = point.x * scale, sy = point.y * scale;
                left += sx < -x;
                right += sx > x;
                below += sy < -y;
                above += sy > y;
                nearest = std::min(nearest, point.z);
            }
            // Partly behind the eye, it may come into view anywhere.
            if (behind == 3 || (behind == 0 && (left == 3 || right == 3 || below == 3 || above == 3))) { continue; }
            const auto size = projection.parallel ? view.pixel / projection.focalLength : view.pixel * std::max(nearest, nearPlane) / projection.focalLength;
            finest = std::min(finest, size);
        }
        return std::isfinite(finest) ? std::optional(finest) : std::nullopt;
    }

private:
    // How many times a face may be halved, and after how many halvings a
    // continuous bend has cut every piece far smaller than its face.
    static constexpr int deepest = 40, deepAfter = deepest - 12;
    // Where an edge's middle lands within this (squared, in pixels) of one of
    // its ends, however long the edge, the bend jumps across it.
    static constexpr double jumpLength = 1.0e-4;

    // Squared lengths in pixels of `size`.
    static double lengthSquared(const DiceCorner& u, const DiceCorner& v, double size) {
        const auto offset = v.at - u.at;
        return offset.dot(offset) / (size * size);
    }
    // Whether the bend jumps across an edge (a step Bit crush makes, or a
    // stripe Polygon snaps to): halving it would never shorten it, as its
    // middle lands on one of its ends, or, once pieces are `deep`, one of
    // its halves is nearly as long as it. A continuous bend shortens both.
    static bool jumps(const DiceCorner& u, const DiceCorner& v, const DiceCorner& middle, bool deep, double size) {
        const auto first = lengthSquared(u, middle, size), second = lengthSquared(middle, v, size);
        if (std::min(first, second) < jumpLength) { return true; }
        return deep && std::max(first, second) > .81 * lengthSquared(u, v, size);
    }

    static osci::Point mix(const osci::Point& from, const osci::Point& u, const osci::Point& v, double s, double t) {
        const auto at = [&](float osci::Point::* axis) { return static_cast<float>(from.*axis + (u.*axis - from.*axis) * s + (v.*axis - from.*axis) * t); };
        return osci::Point(at(&osci::Point::x), at(&osci::Point::y), at(&osci::Point::z));
    }
    static Vec3 mix(const Vec3& from, const Vec3& u, const Vec3& v, double s, double t) { return from + (u - from) * s + (v - from) * t; }

    // Halves until no edge is longer than `limit` (squared, in pixels), the
    // longest first, but never one the bend jumps across: a piece whose long
    // edges all jump is drawn as it is, like a step's riser. A piece small
    // enough to grid is gridded unless it came from a grid (halving it never
    // grids again, so the grid's points are never overwritten while in use).
    // A grid's pieces keep its footprint (`known`): none is nearer an eye.
    template <typename Bend, typename Emit>
    void halve(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, int depthLeft, bool mayGrid, double limit, std::optional<double> known, Bend& bend, Emit& emit) {
        if (stopped) { return; }
        const auto size = known.has_value() ? known : footprint(p.at, q.at, r.at);
        if (!size.has_value()) { return; }
        const auto pq = lengthSquared(p, q, *size), qr = lengthSquared(q, r, *size), rp = lengthSquared(r, p, *size);
        const auto longest = std::max({pq, qr, rp});
        if (longest <= limit || depthLeft <= 0 || ++spent > budget) {
            emit(p, q, r);
            return;
        }
        if (mayGrid && longest <= gridReach * gridReach) {
            grid(p, q, r, std::sqrt(longest), *size, depthLeft, bend, emit);
            return;
        }
        // A middle the bend can't place is left where the flat edge has it.
        const auto middle = [&](const DiceCorner& u, const DiceCorner& v) {
            return midpoints.at(u.source, v.source, [&] {
                const auto point = osci::Point((u.source.x + v.source.x) * .5f, (u.source.y + v.source.y) * .5f, (u.source.z + v.source.z) * .5f);
                return DiceCorner {point, bend(point).value_or((u.at + v.at) * .5)};
            });
        };
        // Edges by length, longest first (the first of equals kept first):
        // 0 is p to q, 1 is q to r, 2 is r to p.
        std::array<std::pair<double, int>, 3> edges {{{pq, 0}, {qr, 1}, {rp, 2}}};
        std::stable_sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [length, edge] : edges) {
            if (length <= limit) { break; }
            const auto& u = edge == 0 ? p : edge == 1 ? q : r;
            const auto& v = edge == 0 ? q : edge == 1 ? r : p;
            const auto m = middle(u, v);
            if (jumps(u, v, m, depthLeft <= deepAfter, *size)) { continue; }
            if (edge == 0) {
                halve(p, m, r, depthLeft - 1, mayGrid, limit, std::nullopt, bend, emit);
                halve(m, q, r, depthLeft - 1, mayGrid, limit, std::nullopt, bend, emit);
            } else if (edge == 1) {
                halve(p, q, m, depthLeft - 1, mayGrid, limit, std::nullopt, bend, emit);
                halve(p, m, r, depthLeft - 1, mayGrid, limit, std::nullopt, bend, emit);
            } else {
                halve(p, q, m, depthLeft - 1, mayGrid, limit, std::nullopt, bend, emit);
                halve(m, q, r, depthLeft - 1, mayGrid, limit, std::nullopt, bend, emit);
            }
            return;
        }
        emit(p, q, r);
    }

    // How far the bend takes a piece off the flat triangle between its
    // corners, at a few points of it, in units of what may be drawn flat: a
    // piece under 1 is flat enough to draw whole, and as the error shrinks
    // with the square of a piece's size, one over 1 is flat enough cut into
    // its square root as many pieces along each edge.
    template <typename Bend>
    static double curving(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, double size, Bend& bend) {
        double worst = 0;
        const auto measure = [&](double s, double t) {
            const auto point = bend(mix(p.source, q.source, r.source, s, t));
            if (!point.has_value()) {
                worst = std::numeric_limits<double>::infinity();
                return;
            }
            worst = std::max(worst, (*point - mix(p.at, q.at, r.at, s, t)).length() / (flatTolerance * size));
        };
        // Each edge's middle, the middle of the piece, and halfway from there
        // to each corner.
        measure(.5, 0);
        measure(.5, .5);
        measure(0, .5);
        measure(1.0 / 3, 1.0 / 3);
        measure(1.0 / 6, 1.0 / 6);
        measure(2.0 / 3, 1.0 / 6);
        measure(1.0 / 6, 2.0 / 3);
        return worst;
    }

    // An even grid of n x n pieces, each point bent once: n from how the bend
    // curves the piece, never finer than a cell. Points are kept row by row:
    // row j holds n + 1 - j of them. A piece the bend still stretches past
    // half as long again as the grid's pieces is halved on.
    template <typename Bend, typename Emit>
    void grid(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, double longest, double size, int depthLeft, Bend& bend, Emit& emit) {
        const auto finest = std::max(2.0, std::ceil(longest / cell));
        // Endless (or not a number) where it cannot be measured: as fine as a cell.
        const auto needed = std::ceil(std::sqrt(curving(p, q, r, size, bend)));
        const auto n = static_cast<int>(needed < finest ? std::max(1.0, needed) : finest);
        const auto stretch = std::max(cell, 1.5 * longest / n);
        points.resize(static_cast<std::size_t>(gridIndex(n, n + 1, 0)));
        for (int j = 0; j <= n; ++j) {
            for (int i = 0; i + j <= n; ++i) {
                auto& point = points[static_cast<std::size_t>(gridIndex(n, j, i))];
                if (i == 0 && j == 0) {
                    point = p;
                } else if (i == n) {
                    point = q;
                } else if (j == n) {
                    point = r;
                } else {
                    const auto s = static_cast<double>(i) / n, t = static_cast<double>(j) / n;
                    point.source = mix(p.source, q.source, r.source, s, t);
                    point.at = bend(point.source).value_or(mix(p.at, q.at, r.at, s, t));
                }
            }
        }
        const auto at = [&](int i, int j) -> const DiceCorner& { return points[static_cast<std::size_t>(gridIndex(n, j, i))]; };
        const auto piece = [&](const DiceCorner& u, const DiceCorner& v, const DiceCorner& w) { halve(u, v, w, depthLeft - 1, false, stretch * stretch, size, bend, emit); };
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i + j < n; ++i) {
                piece(at(i, j), at(i + 1, j), at(i, j + 1));
                if (i + j + 1 < n) { piece(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)); }
            }
        }
    }

    std::span<const DiceView> views;
    MidpointCache<DiceCorner> midpoints;
    bool stopped = false;
    std::size_t spent = 0;
    std::vector<DiceCorner> points;
};
}
