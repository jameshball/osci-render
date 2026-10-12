#pragma once

#include "../model/Vec3.h"
#include "SurfaceLines.h"
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
// density. The curve is laid either on each surface, from the six sides of a
// box round it, so it moves with it (onSurface), or over the frame, fixed
// to it (Field). Pure geometry, so it can be tested alone.
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

// Laid on a surface, from the six sides of a box round it, as a texture
// would be: each face takes the side it faces most squarely, so the curve
// moves and turns with the surface.
namespace onSurface {
// The surface as each side sees it, worked out once for a mesh: its finest
// cells (2^deepest across each side), each with the face its middle is on
// (the outermost, along the side's axis) and where on it, and the faces
// near each part of the side, to cut the curve's steps to.
struct Sides {
    struct Cell {
        std::int32_t face = -1;
        surface::Weights weights {};
    };
    struct Side {
        std::size_t axis = 0;
        double toward = 1;
        std::vector<Cell> cells;
        // Faces by the bucket their box covers, `bucketsAcross` a side.
        std::vector<std::vector<std::uint32_t>> buckets;
    };
    static constexpr int bucketsAcross = 32;
    surface::detail::Bounds bounds;
    int deepest = 0, count = 1;
    std::array<Side, 6> sides;

    // A point of the surface on the side's plane, in cells.
    std::array<double, 2> onPlane(const Side& side, Vec3 point) const {
        const auto u = (side.axis + 1) % 3, v = (side.axis + 2) % 3;
        return {(point[u] - bounds.low[u]) / bounds.size() * count, (point[v] - bounds.low[v]) / bounds.size() * count};
    }

    static Sides of(const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces, int deepest) {
        Sides made;
        made.bounds = surface::detail::boundsOf(corners, faces);
        made.deepest = deepest;
        made.count = 1 << deepest;
        const auto cells = static_cast<std::size_t>(made.count) * static_cast<std::size_t>(made.count);
        for (std::size_t index = 0; index < 6; ++index) {
            auto& side = made.sides[index];
            side.axis = index / 2;
            side.toward = index % 2 == 0 ? 1.0 : -1.0;
            side.cells.assign(cells, {});
            side.buckets.assign(static_cast<std::size_t>(bucketsAcross * bucketsAcross), {});
        }
        // How far out along the side's axis each cell's face is.
        std::array<std::vector<double>, 6> outermost;
        for (auto& depth : outermost) { depth.assign(cells, -1.0e300); }
        for (std::uint32_t index = 0; index < faces.size(); ++index) {
            const auto& f = faces[index];
            const auto normal = (corners[f[1]] - corners[f[0]]).cross(corners[f[2]] - corners[f[0]]);
            const std::array<double, 3> size {std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
            if (!(size[0] + size[1] + size[2] > 0)) { continue; }
            const auto axis = size[0] >= size[1] && size[0] >= size[2] ? std::size_t {0} : size[1] >= size[2] ? std::size_t {1} : std::size_t {2};
            const auto sideIndex = axis * 2 + (normal[axis] > 0 ? 0 : 1);
            auto& side = made.sides[sideIndex];
            const auto a = made.onPlane(side, corners[f[0]]), b = made.onPlane(side, corners[f[1]]), c = made.onPlane(side, corners[f[2]]);
            const auto area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            if (std::abs(area) < 1.0e-12) { continue; }
            const auto left = std::min({a[0], b[0], c[0]}), right = std::max({a[0], b[0], c[0]}), bottom = std::min({a[1], b[1], c[1]}), top = std::max({a[1], b[1], c[1]});
            const auto scale = static_cast<double>(bucketsAcross) / made.count;
            for (auto y = std::clamp(static_cast<int>(bottom * scale), 0, bucketsAcross - 1); y <= std::clamp(static_cast<int>(top * scale), 0, bucketsAcross - 1); ++y) {
                for (auto x = std::clamp(static_cast<int>(left * scale), 0, bucketsAcross - 1); x <= std::clamp(static_cast<int>(right * scale), 0, bucketsAcross - 1); ++x) {
                    side.buckets[static_cast<std::size_t>(y * bucketsAcross + x)].push_back(index);
                }
            }
            for (auto y = std::max(0, static_cast<int>(std::floor(bottom - .5))); y <= std::min(made.count - 1, static_cast<int>(std::ceil(top - .5))); ++y) {
                for (auto x = std::max(0, static_cast<int>(std::floor(left - .5))); x <= std::min(made.count - 1, static_cast<int>(std::ceil(right - .5))); ++x) {
                    const auto px = x + .5, py = y + .5;
                    const auto wb = ((px - a[0]) * (c[1] - a[1]) - (py - a[1]) * (c[0] - a[0])) / area;
                    const auto wc = ((b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])) / area;
                    const auto wa = 1 - wb - wc;
                    constexpr double slack = -1.0e-9;
                    if (wa < slack || wb < slack || wc < slack) { continue; }
                    const auto cell = static_cast<std::size_t>(y) * static_cast<std::size_t>(made.count) + static_cast<std::size_t>(x);
                    const auto out = (corners[f[0]][axis] * wa + corners[f[1]][axis] * wb + corners[f[2]][axis] * wc) * side.toward;
                    if (out <= outermost[sideIndex][cell]) { continue; }
                    outermost[sideIndex][cell] = out;
                    side.cells[cell] = {static_cast<std::int32_t>(index), {wa, wb, wc}};
                }
            }
        }
        return made;
    }
};

// The most light in each cell of a side at each depth, from its finest
// cells' (negative where there is no surface to light).
struct Pyramid {
    std::vector<std::vector<float>> levels;
    explicit Pyramid(std::vector<float> finest, int deepest) {
        levels.resize(static_cast<std::size_t>(deepest) + 1);
        levels[static_cast<std::size_t>(deepest)] = std::move(finest);
        for (auto level = deepest - 1; level >= 0; --level) {
            const auto width = 1 << level;
            const auto& finer = levels[static_cast<std::size_t>(level) + 1];
            auto& here = levels[static_cast<std::size_t>(level)];
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
    float at(int level, int row, int column) const {
        const auto width = 1 << level;
        return levels[static_cast<std::size_t>(level)][static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)];
    }
};

// A surface lit for a frame: each side's cells' light, as a share of the
// brightest (see Field), negative where nothing is or it faces away.
struct Lit {
    const Sides* sides = nullptr;
    std::vector<Pyramid> pyramids;
    // `light(face, weights)`, for each side's cells with a face.
    template <typename Light>
    static Lit of(const Sides& sides, Light&& light) {
        Lit lit;
        lit.sides = &sides;
        for (const auto& side : sides.sides) {
            std::vector<float> finest(side.cells.size(), -1.0f);
            for (std::size_t index = 0; index < side.cells.size(); ++index) {
                const auto& cell = side.cells[index];
                if (cell.face >= 0) { finest[index] = static_cast<float>(light(static_cast<std::uint32_t>(cell.face), cell.weights)); }
            }
            lit.pyramids.emplace_back(std::move(finest), sides.deepest);
        }
        return lit;
    }
    float brightest() const {
        float most = 0;
        for (const auto& pyramid : pyramids) { most = std::max(most, pyramid.levels.front().front()); }
        return most;
    }

    // The curve on each side: `visit(side, cell middle or none, light)` for
    // each of its cells in order, a cell divided while it is wider than the
    // finest (`across` of them a side) over its light (out of `exposure`)
    // times `gain`, at least to a 4 by 4 grid.
    template <typename Visit>
    void curve(int across, double exposure, double gain, Visit&& visit) const {
        constexpr int shallowest = 2;
        const auto count = sides->count;
        const auto finest = static_cast<double>(count) / std::max(1, across);
        for (std::size_t index = 0; index < 6; ++index) {
            const auto& pyramid = pyramids[index];
            const auto walk = [&](const auto& self, double x, double y, double ix, double iy, double jx, double jy, int level) -> void {
                const auto cx = x + (ix + jx) / 2, cy = y + (iy + jy) / 2;
                const auto size = std::abs(ix) + std::abs(iy);
                const auto width = 1 << level;
                const auto column = std::clamp(static_cast<int>(std::floor(cx / size)), 0, width - 1), row = std::clamp(static_cast<int>(std::floor(cy / size)), 0, width - 1);
                const auto most = pyramid.at(level, row, column);
                const auto light = most < 0 ? -1.0 : std::clamp(most / exposure, 0.0, 1.0);
                const auto divide = level < sides->deepest && (level < shallowest || (light > 0 && size * light * gain > finest * detail::threshold(cx / count + static_cast<double>(index) * 7.31, cy / count, level)));
                if (divide) {
                    const auto hx = ix / 2, hy = iy / 2, kx = jx / 2, ky = jy / 2;
                    self(self, x, y, kx, ky, hx, hy, level + 1);
                    self(self, x + hx, y + hy, hx, hy, kx, ky, level + 1);
                    self(self, x + hx + kx, y + hy + ky, hx, hy, kx, ky, level + 1);
                    self(self, x + hx + jx, y + hy + jy, -kx, -ky, -hx, -hy, level + 1);
                    return;
                }
                visit(index, light >= 0 ? std::optional<std::array<double, 2>>(std::array<double, 2> {cx, cy}) : std::nullopt, light);
            };
            walk(walk, 0, 0, count, 0, 0, count, 0);
        }
    }
};

// Where a step of the curve on a side, from p to q (in cells), lies on the
// side's faces: pieces in order along it.
inline std::vector<surface::Piece> cut(const Sides& sides, std::size_t sideIndex, const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces, std::array<double, 2> p, std::array<double, 2> q) {
    const auto& side = sides.sides[sideIndex];
    const auto scale = static_cast<double>(Sides::bucketsAcross) / sides.count;
    std::vector<std::uint32_t> near;
    const auto x0 = std::clamp(static_cast<int>(std::min(p[0], q[0]) * scale), 0, Sides::bucketsAcross - 1), x1 = std::clamp(static_cast<int>(std::max(p[0], q[0]) * scale), 0, Sides::bucketsAcross - 1);
    const auto y0 = std::clamp(static_cast<int>(std::min(p[1], q[1]) * scale), 0, Sides::bucketsAcross - 1), y1 = std::clamp(static_cast<int>(std::max(p[1], q[1]) * scale), 0, Sides::bucketsAcross - 1);
    for (auto y = y0; y <= y1; ++y) {
        for (auto x = x0; x <= x1; ++x) {
            const auto& bucket = side.buckets[static_cast<std::size_t>(y * Sides::bucketsAcross + x)];
            near.insert(near.end(), bucket.begin(), bucket.end());
        }
    }
    std::sort(near.begin(), near.end());
    near.erase(std::unique(near.begin(), near.end()), near.end());
    std::vector<std::pair<double, surface::Piece>> along;
    for (const auto index : near) {
        const auto& f = faces[index];
        const auto a = sides.onPlane(side, corners[f[0]]), b = sides.onPlane(side, corners[f[1]]), c = sides.onPlane(side, corners[f[2]]);
        const auto area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        if (std::abs(area) < 1.0e-12) { continue; }
        const auto weightsAt = [&](const std::array<double, 2>& point) {
            const auto wb = ((point[0] - a[0]) * (c[1] - a[1]) - (point[1] - a[1]) * (c[0] - a[0])) / area;
            const auto wc = ((b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0])) / area;
            return surface::Weights {1 - wb - wc, wb, wc};
        };
        const auto wp = weightsAt(p), wq = weightsAt(q);
        double low = 0, high = 1;
        for (std::size_t corner = 0; corner < 3 && low < high; ++corner) {
            const auto start = wp[corner], end = wq[corner];
            if (start < 0 && end < 0) {
                high = -1;
                break;
            }
            if (start < 0) { low = std::max(low, start / (start - end)); }
            if (end < 0) { high = std::min(high, start / (start - end)); }
        }
        if (!(high > low)) { continue; }
        const auto mix = [&](double share) {
            surface::Weights weights {};
            for (std::size_t corner = 0; corner < 3; ++corner) { weights[corner] = wp[corner] + (wq[corner] - wp[corner]) * share; }
            return weights;
        };
        along.emplace_back(low, surface::Piece {index, mix(low), mix(high)});
    }
    std::sort(along.begin(), along.end(), [](const auto& one, const auto& other) { return one.first < other.first; });
    std::vector<surface::Piece> pieces;
    for (const auto& [where, piece] : along) { pieces.push_back(piece); }
    return pieces;
}
}
}
