#pragma once

#include "../model/Vec3.h"
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
// it is in the scene, and how much light reaches it (0..1).
struct Seen {
    std::uint32_t surface;
    Vec3 at;
    double light;
    std::array<float, 3> hue {1, 1, 1};
};

namespace detail {
// A cell's own threshold, from 1 to 2: by where its middle is and how deep
// it is, the same from frame to frame.
inline double threshold(double x, double y, int level) {
    auto hash = static_cast<std::uint64_t>(std::llround(x * 1.0e6)) * 0x9e3779b97f4a7c15ull ^ static_cast<std::uint64_t>(std::llround(y * 1.0e6)) * 0xc2b2ae3d27d4eb4full ^ static_cast<std::uint64_t>(level) * 0x165667b19e3779f9ull;
    hash ^= hash >> 31;
    hash *= 0xbf58476d1ce4e5b9ull;
    hash ^= hash >> 29;
    return 1 + static_cast<double>(hash >> 11) / static_cast<double>(1ull << 53);
}
}

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
    std::vector<std::vector<float>> brightest;

    Field(double halfWidth, int across, Look looking) : look(std::move(looking)), half(halfWidth), finest(2 * halfWidth / std::max(1, across)) {
        deepest = std::clamp(static_cast<int>(std::ceil(std::log2(std::max(1, across)))), 2, 9);
        cells = 1 << deepest;
        step = 2 * half / cells;
        seen.resize(static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells));
        brightest.resize(static_cast<std::size_t>(deepest) + 1);
        brightest[static_cast<std::size_t>(deepest)].assign(seen.size(), -1.0f);
        for (int row = 0; row < cells; ++row) {
            for (int column = 0; column < cells; ++column) {
                const auto index = static_cast<std::size_t>(row) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(column);
                seen[index] = look(-half + (column + .5) * step, -half + (row + .5) * step);
                if (seen[index].has_value()) { exposure = std::max(exposure, seen[index]->light); }
            }
        }
        for (std::size_t index = 0; index < seen.size(); ++index) {
            if (!seen[index].has_value()) { continue; }
            seen[index]->light = exposed(seen[index]->light);
            brightest[static_cast<std::size_t>(deepest)][index] = static_cast<float>(seen[index]->light);
        }
        for (auto level = deepest - 1; level >= 0; --level) {
            const auto width = 1 << level;
            const auto& finer = brightest[static_cast<std::size_t>(level) + 1];
            auto& here = brightest[static_cast<std::size_t>(level)];
            here.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(width), -1.0f);
            for (int row = 0; row < width; ++row) {
                for (int column = 0; column < width; ++column) {
                    auto& most = here[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)];
                    for (const auto [dx, dy] : {std::pair {0, 0}, std::pair {1, 0}, std::pair {0, 1}, std::pair {1, 1}}) {
                        most = std::max(most, finer[static_cast<std::size_t>(row * 2 + dy) * static_cast<std::size_t>(width * 2) + static_cast<std::size_t>(column * 2 + dx)]);
                    }
                }
            }
        }
    }

    // The curve: `visit(seen, size)` is told of each of its cells in order
    // (none for one with no surface in it) and how wide it is, and it runs
    // through those that follow on. Cells are divided while wider than the
    // finest over the light in them times `gain`, at least to a 4 by 4 grid.
    // A cell's point is what is seen at its middle, or failing that its
    // finest cell nearest the middle that has a surface; `placed` false
    // leaves them unlooked for, as for counting.
    template <typename Visit>
    void curve(double gain, Visit&& visit, bool placed = true) {
        constexpr int shallowest = 2;
        // A cell from its corner (x, y) along i and j, as the curve by vectors goes.
        const auto walk = [&](const auto& self, double x, double y, double ix, double iy, double jx, double jy, int level) -> void {
            const auto cx = x + (ix + jx) / 2, cy = y + (iy + jy) / 2;
            const auto size = std::abs(ix) + std::abs(iy);
            const auto width = 1 << level;
            const auto column = std::clamp(static_cast<int>(std::floor((cx + half) / size)), 0, width - 1), row = std::clamp(static_cast<int>(std::floor((cy + half) / size)), 0, width - 1);
            const auto light = brightest[static_cast<std::size_t>(level)][static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)];
            const auto divide = level < deepest && (level < shallowest || (light > 0 && size * light * gain > finest * detail::threshold(cx, cy, level)));
            if (divide) {
                // Hilbert's order: the first quarter turned one way, the
                // middle two as the whole, the last turned the other.
                const auto hx = ix / 2, hy = iy / 2, kx = jx / 2, ky = jy / 2;
                self(self, x, y, kx, ky, hx, hy, level + 1);
                self(self, x + hx, y + hy, hx, hy, kx, ky, level + 1);
                self(self, x + hx + kx, y + hy + ky, hx, hy, kx, ky, level + 1);
                self(self, x + hx + jx, y + hy + jy, -kx, -ky, -hx, -hy, level + 1);
                return;
            }
            if (light < 0) {
                visit(std::optional<Seen>(), size, light);
                return;
            }
            if (!placed) {
                visit(std::optional<Seen>(), size, light);
                return;
            }
            auto centre = level == deepest ? seen[static_cast<std::size_t>(row) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(column)] : look(cx, cy);
            if (level != deepest && centre.has_value()) { centre->light = exposed(centre->light); }
            if (!centre.has_value()) {
                // The finest cell in it nearest its middle that has a surface.
                const auto span = cells >> level;
                double nearest = 1.0e300;
                for (int r = row * span; r < (row + 1) * span; ++r) {
                    for (int c = column * span; c < (column + 1) * span; ++c) {
                        const auto& candidate = seen[static_cast<std::size_t>(r) * static_cast<std::size_t>(cells) + static_cast<std::size_t>(c)];
                        if (!candidate.has_value()) { continue; }
                        const auto distance = std::hypot(-half + (c + .5) * step - cx, -half + (r + .5) * step - cy);
                        if (distance < nearest) {
                            nearest = distance;
                            centre = candidate;
                        }
                    }
                }
            }
            visit(centre, size, light);
        };
        walk(walk, -half, -half, 2 * half, 0, 0, 2 * half, 0);
    }
};

// The curve over the square `half` either side of the middle of the frame,
// `look(x, y)` saying what is seen at a point: `visit(seen, size)` is told of
// each of its cells in order (see Field::curve).
template <typename Look, typename Visit>
void curve(double half, int across, Look&& look, Visit&& visit) {
    Field field(half, across, std::forward<Look>(look));
    field.curve(1, [&](const std::optional<Seen>& seen, double size, float) { visit(seen, size); });
}

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
    auto gain = 1.0;
    if (pointsAt(1) > most) {
        double low = 0, high = 1;
        for (int halving = 0; halving < 10; ++halving) {
            const auto middle = (low + high) / 2;
            (pointsAt(middle) > most ? high : low) = middle;
        }
        gain = low;
    }
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
