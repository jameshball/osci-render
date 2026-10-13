#pragma once

#include "../model/Vec3.h"
#include "HilbertCurve.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

// Shading by a curve's density rather than brightness: a Hilbert curve, its
// cells divided finer where more light falls, so a lit surface fills with
// curve and a dark one has little. Each cell's threshold for dividing is its
// own, fixed to where the cell is, so as light grows the cells divide one by
// one rather than all at once, and a gradient of light is a gradient of
// density. The curve is Hilbert's over the frame, fixed to it (Field). Pure
// geometry, so it can be tested alone.
namespace motion::hidden::hatching {
// What the curve finds at a point of the frame: the surface seen there, where
// it is in the scene, and how much light reaches it (as a share of the
// brightest seen, 0..1, once the field has it).
struct Seen {
    std::uint32_t surface;
    Vec3 at;
    double light;
    std::array<float, 3> hue {1, 1, 1};
};

// What is seen over the square `half` either side of the middle of the
// frame, looked for once in each of its finest cells (`across` of them
// across, to a power of two), and the most light in each cell at each depth,
// so a cell knows the most light anywhere in it, however little of it a
// surface fills. Light is taken as a share of the brightest seen, as an
// exposure: the brightest surface is hatched densest, however bright.
template <typename Look>
struct Field {
    Look look;
    double half, finest, step;
    // The brightest light seen.
    double exposure = 0;
    double exposed(double light) const { return exposure > 0 ? std::clamp(light / exposure, 0.0, 1.0) : 0.0; }
    int deepest, cells;
    std::vector<std::optional<Seen>> seen;
    // The most light in each cell, at each depth (-1 where nothing is seen).
    hilbert::Pyramid brightest;

    Field(double halfWidth, int across, Look looking) : look(std::move(looking)), half(halfWidth), finest(2 * halfWidth / std::max(1, across)) {
        deepest = std::clamp(static_cast<int>(std::ceil(std::log2(std::max(1, across)))), 2, 9);
        cells = 1 << deepest;
        step = 2 * half / cells;
        seen.resize(static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells));
        for (int row = 0; row < cells; ++row) {
            for (int column = 0; column < cells; ++column) {
                const auto index = static_cast<std::size_t>(row) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(column);
                seen[index] = look(-half + (column + .5) * step, -half + (row + .5) * step);
                if (seen[index].has_value()) { exposure = std::max(exposure, seen[index]->light); }
            }
        }
        std::vector<float> finestLight(seen.size(), -1.0f);
        for (std::size_t index = 0; index < seen.size(); ++index) {
            if (!seen[index].has_value()) { continue; }
            seen[index]->light = exposed(seen[index]->light);
            finestLight[index] = static_cast<float>(seen[index]->light);
        }
        brightest = hilbert::Pyramid::of(std::move(finestLight), deepest, hilbert::Pyramid::Combine::most);
    }

    // The curve (see hilbert::adaptive): `visit(seen, size, light)` is told
    // of each of its cells in order (none seen for one with no surface in
    // it), how wide it is, and the most light in it. Cells are divided while
    // wider than the finest over the light in them times `gain`, at least to
    // a 4 by 4 grid. A cell's point is what its finest cells nearest its
    // middle see; `placed` false leaves them unlooked for, as for counting.
    template <typename Visit>
    void curve(double gain, Visit&& visit, bool placed = true) {
        hilbert::adaptive(brightest, gain, finest / step, [&](const hilbert::Cell& cell) {
            const auto light = brightest.at(cell.level, cell.row, cell.column);
            const auto size = cell.size * step;
            if (light < 0 || !placed) {
                visit(std::optional<Seen>(), size, light);
                return;
            }
            visit(nearestSeen(cell), size, light);
        });
    }

private:
    const std::optional<Seen>& seenAt(int row, int column) const { return seen[static_cast<std::size_t>(row) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(column)]; }
    // What a cell's finest cells nearest its middle see: those at its middle
    // if any has a surface, else the nearest that has.
    std::optional<Seen> nearestSeen(const hilbert::Cell& cell) const {
        if (cell.level == deepest) { return seenAt(cell.row, cell.column); }
        const auto span = cells >> cell.level;
        const auto nearestIn = [&](int top, int left, int bottom, int right) {
            std::optional<Seen> found;
            double nearest = 1.0e300;
            for (int r = top; r < bottom; ++r) {
                for (int c = left; c < right; ++c) {
                    const auto& candidate = seenAt(r, c);
                    if (!candidate.has_value()) { continue; }
                    const auto distance = std::hypot(c + .5 - cell.x, r + .5 - cell.y);
                    if (distance < nearest) {
                        nearest = distance;
                        found = candidate;
                    }
                }
            }
            return found;
        };
        const auto middleRow = cell.row * span + span / 2, middleColumn = cell.column * span + span / 2;
        auto found = nearestIn(middleRow - 1, middleColumn - 1, middleRow + 1, middleColumn + 1);
        if (!found.has_value()) { found = nearestIn(cell.row * span, cell.column * span, (cell.row + 1) * span, (cell.column + 1) * span); }
        return found;
    }
};

// Light too little to draw: none of the curve shows there.
inline constexpr double darkest = .04;

// The curve as strokes on each surface: runs of cells that follow on, on one
// surface and lit, their points in the scene, with how much each is lit.
struct Stroke {
    std::uint32_t surface;
    std::vector<Seen> points;
};
template <typename Look>
std::vector<Stroke> strokes(double half, int across, Look&& look, std::size_t most = static_cast<std::size_t>(-1)) {
    Field field(half, across, std::forward<Look>(look));
    // As dense as the light asks, or less, evenly, so it takes no more than
    // `most` points: the beam has so many in a cycle.
    const auto pointsAt = [&](double gain) {
        std::size_t count = 0;
        field.curve(gain, [&](const std::optional<Seen>&, double, float light) { count += light >= darkest ? 1 : 0; }, false);
        return count;
    };
    // None fits: the coarsest curve.
    const auto gain = hilbert::fitting(most, pointsAt).value_or(0.0);
    std::vector<Stroke> found;
    bool open = false;
    field.curve(gain, [&](const std::optional<Seen>& seen, double, float) {
        if (!seen.has_value() || seen->light < darkest) {
            open = false;
            return;
        }
        if (!open || found.back().surface != seen->surface) {
            found.push_back({seen->surface, {}});
            open = true;
        }
        found.back().points.push_back(*seen);
    });
    // A stroke of one point draws nothing.
    std::erase_if(found, [](const Stroke& stroke) { return stroke.points.size() < 2; });
    return found;
}
}
