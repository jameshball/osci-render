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

// Cuts a face something bends into pieces no edge of which is longer than a
// cell on screen once bent, so each piece is flat enough to draw as a
// triangle. Large pieces are halved across their longest edge, through a new
// point bent like any other, which follows the bend wherever it goes and
// drops whatever falls wholly off the frame. Once a piece is a few cells
// across, it is cut into an even grid instead, each of whose points is bent
// once, and any grid piece the bend still stretches past a cell is halved on.
class Dicer {
public:
    // Edges, in pixels, past which pieces are halved, and past which a piece
    // is too large to grid.
    static constexpr double cell = 4, gridReach = 8 * cell;

    // What the Scope shows: a piece wholly past it is dropped.
    void setReach(Reach value) { reach = value; }

    // `bend` places a source point on screen; `emit` takes each piece.
    template <typename Bend, typename Emit>
    void dice(const DiceCorner& a, const DiceCorner& b, const DiceCorner& c, Bend&& bend, Emit&& emit) {
        midpoints.clear();
        halve(a, b, c, 40, true, bend, emit);
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
    static osci::Point mix(const osci::Point& from, const osci::Point& u, const osci::Point& v, double s, double t) {
        const auto at = [&](float osci::Point::* axis) { return static_cast<float>(from.*axis + (u.*axis - from.*axis) * s + (v.*axis - from.*axis) * t); };
        return osci::Point(at(&osci::Point::x), at(&osci::Point::y), at(&osci::Point::z));
    }

    // Halves until no edge is longer than a cell; a piece small enough to
    // grid is gridded unless it came from a grid (halving it never grids
    // again, so the grid's points are never overwritten while in use).
    template <typename Bend, typename Emit>
    void halve(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, int depthLeft, bool mayGrid, Bend& bend, Emit& emit) {
        if (offFrame(p, q, r)) { return; }
        const auto pq = lengthSquared(p, q), qr = lengthSquared(q, r), rp = lengthSquared(r, p);
        const auto longest = std::max({pq, qr, rp});
        // Partly behind the eye, a piece is halved only so far, then cut
        // where it crosses into view: dropped, it would leave a hole just
        // where a surface comes nearest, at the edges of the view.
        const auto whole = p.at.has_value() && q.at.has_value() && r.at.has_value();
        if (!whole) { depthLeft = std::min(depthLeft, 12); }
        if (longest <= cell * cell || depthLeft <= 0) {
            if (whole) {
                emit(p, q, r);
            } else {
                clipToView(p, q, r, bend, emit);
            }
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
        if (pq >= qr && pq >= rp) {
            const auto m = middle(p, q);
            halve(p, m, r, depthLeft - 1, mayGrid, bend, emit);
            halve(m, q, r, depthLeft - 1, mayGrid, bend, emit);
        } else if (qr >= rp) {
            const auto m = middle(q, r);
            halve(p, q, m, depthLeft - 1, mayGrid, bend, emit);
            halve(p, m, r, depthLeft - 1, mayGrid, bend, emit);
        } else {
            const auto m = middle(r, p);
            halve(p, q, m, depthLeft - 1, mayGrid, bend, emit);
            halve(m, q, r, depthLeft - 1, mayGrid, bend, emit);
        }
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
        if (count >= 3) { emit(kept[0], kept[1], kept[2]); }
        if (count == 4) { emit(kept[0], kept[2], kept[3]); }
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

    // An even grid of n x n pieces, n from the longest edge, each point bent
    // once. Points are kept row by row: row j holds n + 1 - j of them. A
    // piece the bend still stretches past a cell is halved on.
    template <typename Bend, typename Emit>
    void grid(const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, double longest, int depthLeft, Bend& bend, Emit& emit) {
        const auto n = std::max(2, static_cast<int>(std::ceil(longest / cell)));
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
        const auto piece = [&](const DiceCorner& u, const DiceCorner& v, const DiceCorner& w) { halve(u, v, w, depthLeft - 1, false, bend, emit); };
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i + j < n; ++i) {
                piece(at(i, j), at(i + 1, j), at(i, j + 1));
                if (i + j + 1 < n) { piece(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)); }
            }
        }
    }

    MidpointCache<DiceCorner> midpoints;
    std::vector<DiceCorner> points;
    Reach reach;
};
}
