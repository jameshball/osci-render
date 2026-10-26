#pragma once

#include "HiddenLineDepth.h"
#include "HiddenLineMidpoints.h"
#include "Lighting.h"
#include "TrianglePoints.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
// scene, and each view draws the same pieces. A piece's edges are measured in
// pixels of the view that sees it most finely (see measure), and how flat it
// is in every view that sees it (see curving), so it is as fine as the finest
// view needs and no finer; one no view sees is dropped. Large pieces
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
    // How far a piece drawn flat may be from the bent surface: on screen, in
    // pixels, and in depth, as a share of the room a depth test leaves.
    static constexpr double flatTolerance = .125, depthTolerance = DepthBuffer::depthPush / 4;

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
        halve(a, b, c, deepest, true, cell * cell, bend, asked);
        return !stopped;
    }
    template <typename Bend, typename Emit>
    void dice(const DiceCorner& a, const DiceCorner& b, const DiceCorner& c, Bend&& bend, Emit&& emit) {
        dice(a, b, c, bend, emit, [] { return false; });
    }

    // A piece as the views that see it need it: the size of a pixel where
    // it is in the view that sees it most finely (in perspective its nearest
    // corner's depth over the focal length; toward a view's edge a length
    // can land up to half as long again, which the cells' margin allows),
    // and which views see it.
    struct Measure {
        double pixel = std::numeric_limits<double>::infinity();
        std::uint64_t seenBy = 0;
    };
    // None when no view sees it.
    std::optional<Measure> measure(const Vec3& p, const Vec3& q, const Vec3& r) const {
        Measure measured;
        bool any = false;
        for (std::size_t index = 0; index < views.size(); ++index) {
            const auto& view = views[index];
            const auto& projection = view.projection;
            // Wholly past one side of the view (or behind its eye) it doesn't see it.
            const auto x = view.reach.x * DepthBuffer::overscan, y = view.reach.y * DepthBuffer::overscan;
            int left = 0, right = 0, below = 0, above = 0, behind = 0;
            auto nearest = std::numeric_limits<double>::infinity();
            for (const auto* corner : {&p, &q, &r}) {
                const auto seen = view.eye.see(*corner);
                const auto at = projection(seen);
                if (!at.has_value()) {
                    ++behind;
                    continue;
                }
                left += at->x < -x;
                right += at->x > x;
                below += at->y < -y;
                above += at->y > y;
                nearest = std::min(nearest, seen.z);
            }
            // Partly behind the eye, it may come into view anywhere.
            if (behind == 3 || (behind == 0 && (left == 3 || right == 3 || below == 3 || above == 3))) { continue; }
            any = true;
            if (index < 64) { measured.seenBy |= std::uint64_t {1} << index; }
            const auto depth = projection.parallel ? 1.0 : std::max(nearest, nearPlane);
            measured.pixel = std::min(measured.pixel, view.pixel * depth / projection.focalLength);
        }
        return any ? std::optional(measured) : std::nullopt;
    }

private:
    // How many times a face may be halved, and after how many halvings a
    // continuous bend has cut every piece far smaller than its face.
    static constexpr int deepest = 40, deepAfter = deepest - 12;
    // Where an edge's middle lands within this (squared, in pixels) of one of
    // its ends, however long the edge, the bend jumps across it.
    static constexpr double jumpLength = 1.0e-4;

    // An edge's length, squared, in pixels of `size`.
    static double lengthSquared(const Vec3& u, const Vec3& v, double size) {
        const auto offset = v - u;
        return offset.dot(offset) / (size * size);
    }
    // Whether the bend jumps across an edge (a step Bit crush makes, or a
    // stripe Polygon snaps to): halving it would never shorten it, as its
    // middle lands on one of its ends, or, once pieces are `deep`, the half
    // across the step is nearly as long as it and the other, on one side,
    // short. A continuous bend shortens both.
    static bool jumps(const DiceCorner& u, const DiceCorner& v, const DiceCorner& middle, bool deep, double whole, double size) {
        const auto first = lengthSquared(u.at, middle.at, size), second = lengthSquared(middle.at, v.at, size);
        if (std::min(first, second) < jumpLength) { return true; }
        return deep && std::max(first, second) > .81 * whole && std::min(first, second) < .25 * whole;
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
    template <typename Bend, typename Emit>
    void halve(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, int depthLeft, bool mayGrid, double limit, Bend& bend, Emit& emit) {
        if (stopped) { return; }
        const auto measured = measure(p.at, q.at, r.at);
        if (!measured.has_value()) { return; }
        const auto pq = lengthSquared(p.at, q.at, measured->pixel), qr = lengthSquared(q.at, r.at, measured->pixel), rp = lengthSquared(r.at, p.at, measured->pixel);
        const auto longest = std::max({pq, qr, rp});
        if (longest <= limit || depthLeft <= 0 || ++spent > budget) {
            emit(p, q, r);
            return;
        }
        if (mayGrid && longest <= gridReach * gridReach) {
            grid(p, q, r, std::sqrt(longest), measured->seenBy, depthLeft, bend, emit);
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
            if (jumps(u, v, m, depthLeft <= deepAfter, length, measured->pixel)) { continue; }
            if (edge == 0) {
                halve(p, m, r, depthLeft - 1, mayGrid, limit, bend, emit);
                halve(m, q, r, depthLeft - 1, mayGrid, limit, bend, emit);
            } else if (edge == 1) {
                halve(p, q, m, depthLeft - 1, mayGrid, limit, bend, emit);
                halve(p, m, r, depthLeft - 1, mayGrid, limit, bend, emit);
            } else {
                halve(p, q, m, depthLeft - 1, mayGrid, limit, bend, emit);
                halve(m, q, r, depthLeft - 1, mayGrid, limit, bend, emit);
            }
            return;
        }
        emit(p, q, r);
    }

    // Each edge's middle, the middle of the piece, and halfway from there
    // to each corner; an edge's middle is measured from its line too.
    struct Probe {
        double s, t;
        int from, to;
    };
    static constexpr std::array<Probe, 7> probes {{{.5, 0, 0, 1}, {.5, .5, 1, 2}, {0, .5, 2, 0}, {1.0 / 3, 1.0 / 3, -1, -1}, {1.0 / 6, 1.0 / 6, -1, -1}, {2.0 / 3, 1.0 / 6, -1, -1}, {1.0 / 6, 2.0 / 3, -1, -1}}};

    // How far the bend takes a piece off the flat triangle between its
    // corners, at a few points of it (see probes, kept bent for the grid
    // after: where one of its points is a probe, the probe is that point), in units of what may be drawn flat, in
    // the worst of the views that see it (`seenBy`): a piece under 1 is flat
    // enough to draw whole, and as the error shrinks with the square of a
    // piece's size, one over 1 is flat enough cut into its square root as many
    // pieces along each edge. Inverse depth is linear over a view's screen
    // across a flat triangle, so a point's depth is measured from that plane,
    // allowing as much as the depth test does: a share of its depth and of
    // how much the plane's depth changes across a pixel. A point on an edge is
    // also measured from the edge's line, as it shapes the outline the surface
    // hides with. A view that sees the piece partly behind its eye measures
    // nothing (the piece is cut where it comes into view).
    template <typename Bend>
    double curving(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, std::uint64_t seenBy, Bend& bend) {
        constexpr auto endless = std::numeric_limits<double>::infinity();
        probesBent = 0;
        auto& bent = probed;
        for (std::size_t index = 0; index < probes.size(); ++index) {
            const auto point = bend(mix(p.source, q.source, r.source, probes[index].s, probes[index].t));
            if (!point.has_value()) { return endless; }
            bent[index] = *point;
            probesBent = index + 1;
        }
        double worst = 0;
        for (std::size_t index = 0; index < views.size(); ++index) {
            if (index < 64 && (seenBy >> index & 1) == 0) { continue; }
            const auto& view = views[index];
            const auto& projection = view.projection;
            const auto ap = projection(view.eye.see(p.at)), bp = projection(view.eye.see(q.at)), cp = projection(view.eye.see(r.at));
            if (!ap.has_value() || !bp.has_value() || !cp.has_value()) { continue; }
            const std::array<Screen, 3> corners {*ap, *bp, *cp};
            const auto& a = corners[0];
            const auto& b = corners[1];
            const auto& c = corners[2];
            const auto ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
            const auto area = ux * vy - uy * vx;
            // Seen edge on, it has no plane to measure from.
            if (!(std::abs(area) > 0)) { return endless; }
            const auto dwdx = ((b.w - a.w) * vy - (c.w - a.w) * uy) / area;
            const auto dwdy = ((c.w - a.w) * ux - (b.w - a.w) * vx) / area;
            const auto slope = (std::abs(dwdx) + std::abs(dwdy)) * view.pixel;
            for (std::size_t probe = 0; probe < probes.size(); ++probe) {
                const auto point = projection(view.eye.see(bent[probe]));
                if (!point.has_value()) { return endless; }
                const auto plane = a.w + dwdx * (point->x - a.x) + dwdy * (point->y - a.y);
                worst = std::max(worst, std::abs(point->w - plane) / (depthTolerance * std::abs(point->w) + flatTolerance * slope));
                if (probes[probe].from < 0) { continue; }
                const auto& from = corners[static_cast<std::size_t>(probes[probe].from)];
                const auto& to = corners[static_cast<std::size_t>(probes[probe].to)];
                const auto ex = to.x - from.x, ey = to.y - from.y;
                const auto length = std::sqrt(ex * ex + ey * ey);
                if (length > 0) { worst = std::max(worst, std::abs((point->x - from.x) * ey - (point->y - from.y) * ex) / length / (flatTolerance * view.pixel)); }
            }
        }
        return worst;
    }

    // An even grid of n x n pieces, each point bent once: n from how the bend
    // curves the piece, never finer than a cell. Points are kept row by row:
    // row j holds n + 1 - j of them. A piece the bend still stretches past
    // half as long again as the grid's pieces is halved on.
    template <typename Bend, typename Emit>
    void grid(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, double longest, std::uint64_t seenBy, int depthLeft, Bend& bend, Emit& emit) {
        const auto finest = std::max(2.0, std::ceil(longest / cell));
        // Endless (or not a number) where it cannot be measured: as fine as a cell.
        const auto needed = std::ceil(std::sqrt(curving(p, q, r, seenBy, bend)));
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
                    const auto bentProbes = probes.begin() + static_cast<std::ptrdiff_t>(probesBent);
                    const auto probe = std::find_if(probes.begin(), bentProbes, [&](const Probe& each) { return each.s == s && each.t == t; });
                    if (probe != bentProbes) {
                        point.at = probed[static_cast<std::size_t>(probe - probes.begin())];
                    } else {
                        point.at = bend(point.source).value_or(mix(p.at, q.at, r.at, s, t));
                    }
                }
            }
        }
        const auto at = [&](int i, int j) -> const DiceCorner& { return points[static_cast<std::size_t>(gridIndex(n, j, i))]; };
        const auto piece = [&](const DiceCorner& u, const DiceCorner& v, const DiceCorner& w) { halve(u, v, w, depthLeft - 1, false, stretch * stretch, bend, emit); };
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
    std::array<Vec3, probes.size()> probed {};
    std::size_t probesBent = 0;
};
}
