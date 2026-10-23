#pragma once

#include "HiddenLineDepth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace motion::hidden {
// What has been drawn so far in a frame, on screen, so a line is drawn once:
// where one would retrace another (within a pixel, running along it either
// way), the beam would draw the same stroke twice, a little apart and at
// different moments, and it would shimmer. Lines are kept as straight
// segments in a grid of cells a few pixels across. A point is only covered
// away from a drawn line's ends, so a line going on from where another stops,
// or meeting it at a corner, is never taken for it.
class Coverage {
public:
    // How near a line must be to one drawn, and how nearly along it (the sine
    // of the widest angle between them).
    static constexpr double nearby = DepthBuffer::pixel;
    static constexpr double along = .1;
    // How far inside a drawn line's ends a point must be to be covered by it.
    static constexpr double inset = .5 * DepthBuffer::pixel;
    // A drawn line is kept as segments, each straight to within this.
    static constexpr double straight = .25 * DepthBuffer::pixel;

    // Empties it, over what the Scope shows.
    void clear(Reach reach) {
        for (const auto cell : touched) { heads[static_cast<std::size_t>(cell)] = -1; }
        touched.clear();
        entries.clear();
        segments.clear();
        if (reach == covered && !heads.empty()) { return; }
        covered = reach;
        originX = reach.x * DepthBuffer::overscan;
        originY = reach.y * DepthBuffer::overscan;
        columns = static_cast<int>(std::ceil(2 * originX / cell));
        rows = static_cast<int>(std::ceil(2 * originY / cell));
        heads.assign(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), -1);
    }

    // Whether a point, heading (dx, dy) along its line, lies along a line
    // already drawn.
    bool covers(const Screen& point, double dx, double dy) const {
        if (segments.empty()) { return false; }
        const auto heading = std::hypot(dx, dy);
        if (!(heading > 0)) { return false; }
        const auto column = columnOf(point.x), row = rowOf(point.y);
        for (auto y = std::max(0, row - 1); y <= std::min(rows - 1, row + 1); ++y) {
            for (auto x = std::max(0, column - 1); x <= std::min(columns - 1, column + 1); ++x) {
                for (auto entry = heads[static_cast<std::size_t>(y * columns + x)]; entry >= 0; entry = entries[static_cast<std::size_t>(entry)].next) {
                    if (lies(segments[entries[static_cast<std::size_t>(entry)].segment], point, dx / heading, dy / heading)) { return true; }
                }
            }
        }
        return false;
    }

    // Records a line as drawn, from points along it: straight stretches
    // become single segments.
    void add(std::span<const Screen> points) {
        if (points.size() < 2) { return; }
        std::size_t start = 0;
        for (std::size_t index = 2; index < points.size(); ++index) {
            // The straight line to this point leaves the line drawn (most at a
            // curve's middle, or at a corner just passed): the segment ends at
            // the point before.
            const auto stray = std::max(offLine(points[start], points[index], points[(start + index) / 2]), offLine(points[start], points[index], points[index - 1]));
            if (stray > straight) {
                insert(points[start], points[index - 1], start == 0, false);
                start = index - 1;
            }
        }
        insert(points[start], points.back(), start == 0, true);
    }

private:
    struct Segment {
        double ax, ay, bx, by;
        // Whether its ends are the drawn line's.
        bool first, last;
    };
    struct Entry {
        std::uint32_t segment;
        std::int32_t next;
    };
    static constexpr double cell = 4 * DepthBuffer::pixel;

    static double offLine(const Screen& a, const Screen& b, const Screen& p) {
        const auto dx = b.x - a.x, dy = b.y - a.y;
        const auto length = std::hypot(dx, dy);
        if (!(length > 0)) { return std::hypot(p.x - a.x, p.y - a.y); }
        return std::abs(dx * (p.y - a.y) - dy * (p.x - a.x)) / length;
    }
    static bool lies(const Segment& segment, const Screen& point, double ux, double uy) {
        const auto vx = segment.bx - segment.ax, vy = segment.by - segment.ay;
        const auto length = std::hypot(vx, vy);
        // Inside a drawn line's ends; past a join between its segments, as
        // far as a point beside it can be.
        const auto from = segment.first ? inset : -nearby, to = segment.last ? length - inset : length + nearby;
        if (!(to > from)) { return false; }
        // Along it either way, beside it, and away from its ends.
        if (std::abs(vx * uy - vy * ux) > along * length) { return false; }
        const auto px = point.x - segment.ax, py = point.y - segment.ay;
        if (std::abs(vx * py - vy * px) > nearby * length) { return false; }
        const auto t = (px * vx + py * vy) / length;
        return t >= from && t <= to;
    }
    int columnOf(double x) const { return static_cast<int>(std::floor((x + originX) / cell)); }
    int rowOf(double y) const { return static_cast<int>(std::floor((y + originY) / cell)); }
    // Into every cell the segment passes through, stepping half a cell at a
    // time; a point is looked up in its own cell and those around it.
    void insert(const Screen& a, const Screen& b, bool first, bool last) {
        const auto index = static_cast<std::uint32_t>(segments.size());
        segments.push_back({a.x, a.y, b.x, b.y, first, last});
        const auto steps = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / (cell * .5))));
        auto previous = -1;
        for (int step = 0; step <= steps; ++step) {
            const auto t = static_cast<double>(step) / steps;
            const auto column = columnOf(a.x + (b.x - a.x) * t), row = rowOf(a.y + (b.y - a.y) * t);
            if (column < 0 || row < 0 || column >= columns || row >= rows) { continue; }
            const auto at = row * columns + column;
            if (at == previous) { continue; }
            previous = at;
            auto& head = heads[static_cast<std::size_t>(at)];
            if (head < 0) { touched.push_back(at); }
            entries.push_back({index, head});
            head = static_cast<std::int32_t>(entries.size() - 1);
        }
    }

    Reach covered {0, 0};
    double originX = 0, originY = 0;
    int columns = 0, rows = 0;
    std::vector<std::int32_t> heads;
    std::vector<int> touched;
    std::vector<Entry> entries;
    std::vector<Segment> segments;
};
}
