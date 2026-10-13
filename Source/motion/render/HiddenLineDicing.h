#pragma once

#include "HiddenLineDepth.h"
#include "HiddenLineMidpoints.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace motion::hidden {
// A point of a face being diced: where it is in its source, and on screen
// once bent (none behind the eye).
struct DiceCorner {
    osci::Point source;
    std::optional<Screen> at;
};

// Cuts a face something bends into pieces flat enough, once bent, to draw as
// triangles. Large pieces are halved across their longest edge, through a new
// point bent like any other, which follows the bend wherever it goes and
// drops whatever falls wholly off the frame. Once a piece is a few cells
// across, it is cut into an even grid instead, each of whose points is bent
// once: as fine as a cell where the bend curves it most, coarser where it
// keeps the piece flat. A grid piece the bend still stretches is halved on.
class Dicer {
public:
    // Edges, in pixels, past which pieces are halved, and past which a piece
    // is too large to grid.
    static constexpr double cell = 4, gridReach = 8 * cell;
    // How far a piece drawn flat may be from the bent surface: on screen, in
    // pixels, and in depth, as a share of the room a depth test leaves.
    static constexpr double flatTolerance = .125, depthTolerance = DepthBuffer::depthPush / 4;

    // What the Scope shows: a piece wholly past it is dropped.
    void setReach(Reach value) { reach = value; }

    // `bend` places a source point on screen; `emit` takes each piece, and
    // whether it is whole (rather than cut where it comes into view). A face
    // can take many pieces, so `cancelled` is asked every so often; false
    // once it stops the dicing.
    template <typename Bend, typename Emit, typename Cancelled>
    bool dice(const DiceCorner& a, const DiceCorner& b, const DiceCorner& c, Bend&& bend, Emit&& emit, Cancelled&& cancelled) {
        midpoints.clear();
        stopped = false;
        std::size_t emitted = 0;
        auto asked = [&](const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, bool whole) {
            if ((++emitted & 1023) == 0 && cancelled()) { stopped = true; }
            if (!stopped) { emit(p, q, r, whole); }
        };
        halve(a, b, c, deepest, true, cell * cell, bend, asked);
        return !stopped;
    }
    template <typename Bend, typename Emit>
    void dice(const DiceCorner& a, const DiceCorner& b, const DiceCorner& c, Bend&& bend, Emit&& emit) {
        dice(a, b, c, bend, emit, [] { return false; });
    }

private:
    // Squared lengths in pixels; endless where a corner is behind the eye.
    static double lengthSquared(const DiceCorner& u, const DiceCorner& v) {
        if (!u.at.has_value() || !v.at.has_value()) { return std::numeric_limits<double>::infinity(); }
        const auto dx = (v.at->x - u.at->x) / DepthBuffer::pixel, dy = (v.at->y - u.at->y) / DepthBuffer::pixel;
        return dx * dx + dy * dy;
    }
    bool offFrame(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r) const {
        if (!p.at.has_value() && !q.at.has_value() && !r.at.has_value()) { return true; }
        if (!p.at.has_value() || !q.at.has_value() || !r.at.has_value()) { return false; }
        const auto x = reach.x * DepthBuffer::overscan, y = reach.y * DepthBuffer::overscan;
        const auto past = [&](auto&& test) { return test(*p.at) && test(*q.at) && test(*r.at); };
        return past([x](const Screen& s) { return s.x < -x; }) || past([x](const Screen& s) { return s.x > x; })
            || past([y](const Screen& s) { return s.y < -y; }) || past([y](const Screen& s) { return s.y > y; });
    }
    // How many times a face may be halved, and after how many halvings a
    // continuous bend has cut every piece far smaller than its face.
    static constexpr int deepest = 40, deepAfter = deepest - 16;
    // Where an edge's middle lands within this (squared, in pixels) of one of
    // its ends, however long the edge, the bend jumps across it.
    static constexpr double jumpLength = 1.0e-4;

    // Whether the bend jumps across an edge (a step Bit crush makes, or a
    // stripe Polygon snaps to): halving it would never shorten it, as its
    // middle lands on one of its ends, or, once pieces are `deep`, one of
    // its halves is nearly as long as it. A continuous bend shortens both;
    // only perspective keeps one long, where the ends' depths differ widely.
    static bool jumps(const DiceCorner& u, const DiceCorner& v, const DiceCorner& middle, bool deep) {
        if (!u.at.has_value() || !v.at.has_value() || !middle.at.has_value()) { return false; }
        const auto first = lengthSquared(u, middle), second = lengthSquared(middle, v);
        if (std::min(first, second) < jumpLength) { return true; }
        if (!deep) { return false; }
        const auto near = std::max(u.at->w, v.at->w), far = std::min(u.at->w, v.at->w);
        return std::max(first, second) > .81 * lengthSquared(u, v) && near < 1.5 * far;
    }

    static osci::Point mix(const osci::Point& from, const osci::Point& u, const osci::Point& v, double s, double t) {
        const auto at = [&](float osci::Point::* axis) { return static_cast<float>(from.*axis + (u.*axis - from.*axis) * s + (v.*axis - from.*axis) * t); };
        return osci::Point(at(&osci::Point::x), at(&osci::Point::y), at(&osci::Point::z));
    }

    // Halves until no edge is longer than `limit` (squared, in pixels), the
    // longest first, but never one the bend jumps across: a piece whose long
    // edges all jump is drawn as it is, like a step's riser. A piece small
    // enough to grid is gridded unless it came from a grid (halving it never
    // grids again, so the grid's points are never overwritten while in use).
    template <typename Bend, typename Emit>
    void halve(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, int depthLeft, bool mayGrid, double limit, Bend& bend, Emit& emit) {
        if (stopped || offFrame(p, q, r)) { return; }
        const auto pq = lengthSquared(p, q), qr = lengthSquared(q, r), rp = lengthSquared(r, p);
        const auto longest = std::max({pq, qr, rp});
        // Partly behind the eye, a piece is halved only so far, then cut
        // where it crosses into view: dropped, it would leave a hole just
        // where a surface comes nearest, at the edges of the view.
        const auto whole = p.at.has_value() && q.at.has_value() && r.at.has_value();
        if (!whole) { depthLeft = std::min(depthLeft, 12); }
        const auto asItIs = [&] {
            if (whole) {
                emit(p, q, r, true);
            } else {
                clipToView(p, q, r, bend, emit);
            }
        };
        if (longest <= limit || depthLeft <= 0) {
            asItIs();
            return;
        }
        if (mayGrid && whole && longest <= gridReach * gridReach) {
            grid(p, q, r, std::sqrt(longest), depthLeft, bend, emit);
            return;
        }
        const auto middle = [&](const DiceCorner& u, const DiceCorner& v) {
            return midpoints.at(u.source, v.source, [&] {
                const auto point = osci::Point((u.source.x + v.source.x) * .5f, (u.source.y + v.source.y) * .5f, (u.source.z + v.source.z) * .5f);
                return DiceCorner {point, bend(point)};
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
            if (jumps(u, v, m, depthLeft <= deepAfter)) { continue; }
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
        asItIs();
    }

    // The part of a piece in view: its corners in front of the eye and, on
    // each edge that crosses into view, the point it does (found by halving
    // the edge, as the bend need not keep it straight), as one or two pieces.
    template <typename Bend, typename Emit>
    static void clipToView(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, Bend& bend, Emit& emit) {
        const std::array<const DiceCorner*, 3> corners {&p, &q, &r};
        std::array<DiceCorner, 4> kept {};
        std::size_t count = 0;
        for (std::size_t index = 0; index < 3; ++index) {
            const auto& current = *corners[index];
            const auto& next = *corners[(index + 1) % 3];
            if (current.at.has_value()) { kept[count++] = current; }
            if (current.at.has_value() != next.at.has_value()) { kept[count++] = crossing(current, next, bend); }
        }
        if (count >= 3) { emit(kept[0], kept[1], kept[2], false); }
        if (count == 4) { emit(kept[0], kept[2], kept[3], false); }
    }
    // Where an edge from a corner in view to one behind the eye (either way
    // round) comes into view, to well under a pixel.
    template <typename Bend>
    static DiceCorner crossing(const DiceCorner& u, const DiceCorner& v, Bend& bend) {
        const auto& seen = u.at.has_value() ? u : v;
        const auto& unseen = u.at.has_value() ? v : u;
        DiceCorner inside = seen;
        double low = 0, high = 1;
        for (int step = 0; step < 24; ++step) {
            const auto t = (low + high) * .5;
            const auto source = mix(seen.source, unseen.source, seen.source, t, 0);
            const auto at = bend(source);
            if (at.has_value()) {
                inside = {source, at};
                low = t;
            } else {
                high = t;
            }
        }
        return inside;
    }

    // How far the bend takes a piece off the flat triangle between its
    // corners, at a few points of it, in units of what may be drawn flat: a
    // piece under 1 is flat enough to draw whole, and as the error shrinks
    // with the square of a piece's size, one over 1 is flat enough cut into
    // its square root as many pieces along each edge. Inverse depth is linear
    // over the screen across a flat triangle, so a point's depth is measured
    // from that plane; a point on an edge is also measured from the edge's
    // line, as it shapes the outline the surface hides with.
    template <typename Bend>
    double curving(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, Bend& bend) const {
        constexpr auto endless = std::numeric_limits<double>::infinity();
        const auto& a = *p.at;
        const auto& b = *q.at;
        const auto& c = *r.at;
        const auto ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
        const auto area = ux * vy - uy * vx;
        // Seen edge on, it has no plane to measure from.
        if (!(std::abs(area) > 0)) { return endless; }
        const auto dwdx = ((b.w - a.w) * vy - (c.w - a.w) * uy) / area;
        const auto dwdy = ((c.w - a.w) * ux - (b.w - a.w) * vx) / area;
        const auto slope = (std::abs(dwdx) + std::abs(dwdy)) * DepthBuffer::pixel;
        double worst = 0;
        const auto measure = [&](double s, double t, const Screen* from, const Screen* to) {
            const auto point = bend(mix(p.source, q.source, r.source, s, t));
            if (!point.has_value()) {
                worst = endless;
                return;
            }
            const auto plane = a.w + dwdx * (point->x - a.x) + dwdy * (point->y - a.y);
            worst = std::max(worst, std::abs(point->w - plane) / (depthTolerance * std::abs(point->w) + flatTolerance * slope));
            if (from == nullptr) { return; }
            const auto ex = to->x - from->x, ey = to->y - from->y;
            const auto length = std::sqrt(ex * ex + ey * ey);
            if (length > 0) { worst = std::max(worst, std::abs((point->x - from->x) * ey - (point->y - from->y) * ex) / length / (flatTolerance * DepthBuffer::pixel)); }
        };
        // Each edge's middle, the middle of the piece, and halfway from there
        // to each corner.
        measure(.5, 0, &a, &b);
        measure(.5, .5, &b, &c);
        measure(0, .5, &c, &a);
        measure(1.0 / 3, 1.0 / 3, nullptr, nullptr);
        measure(1.0 / 6, 1.0 / 6, nullptr, nullptr);
        measure(2.0 / 3, 1.0 / 6, nullptr, nullptr);
        measure(1.0 / 6, 2.0 / 3, nullptr, nullptr);
        return worst;
    }

    // An even grid of n x n pieces, each point bent once: n from how the bend
    // curves the piece, never finer than a cell. Points are kept row by row:
    // row j holds n + 1 - j of them. A piece the bend still stretches past
    // half as long again as the grid's pieces is halved on.
    template <typename Bend, typename Emit>
    void grid(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, double longest, int depthLeft, Bend& bend, Emit& emit) {
        const auto finest = std::max(2.0, std::ceil(longest / cell));
        // Endless (or not a number) where it cannot be measured: as fine as a cell.
        const auto needed = std::ceil(std::sqrt(curving(p, q, r, bend)));
        const auto n = static_cast<int>(needed < finest ? std::max(1.0, needed) : finest);
        const auto stretch = std::max(cell, 1.5 * longest / n);
        const auto rowStart = [n](int j) { return j * (n + 1) - j * (j - 1) / 2; };
        points.resize(static_cast<std::size_t>(rowStart(n + 1)));
        for (int j = 0; j <= n; ++j) {
            for (int i = 0; i + j <= n; ++i) {
                auto& point = points[static_cast<std::size_t>(rowStart(j) + i)];
                if (i == 0 && j == 0) {
                    point = p;
                } else if (i == n) {
                    point = q;
                } else if (j == n) {
                    point = r;
                } else {
                    point.source = mix(p.source, q.source, r.source, static_cast<double>(i) / n, static_cast<double>(j) / n);
                    point.at = bend(point.source);
                }
            }
        }
        const auto at = [&](int i, int j) -> const DiceCorner& { return points[static_cast<std::size_t>(rowStart(j) + i)]; };
        const auto piece = [&](const DiceCorner& u, const DiceCorner& v, const DiceCorner& w) { halve(u, v, w, depthLeft - 1, false, stretch * stretch, bend, emit); };
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i + j < n; ++i) {
                piece(at(i, j), at(i + 1, j), at(i, j + 1));
                if (i + j + 1 < n) { piece(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)); }
            }
        }
    }

    MidpointCache<DiceCorner> midpoints;
    bool stopped = false;
    std::vector<DiceCorner> points;
    Reach reach;
};
}
