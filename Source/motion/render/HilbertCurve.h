#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>

// A Hilbert curve over a square grid, its cells divided as finely as each
// asks: the one curve both the camera's hatching (over the frame, as dense as
// the light) and images traced by a Hilbert curve are drawn by. Each cell has
// its own threshold for dividing, fixed to where it is, so as what it asks for
// grows the cells divide one by one rather than all at once, and a gradient is
// a gradient of density. Pure geometry, so it can be tested alone.
namespace motion::hilbert {
// A cell's own threshold, from 1 to 2: by where it is and how deep, the same
// from frame to frame.
inline double threshold(int column, int row, int level) {
    auto hash = static_cast<std::uint64_t>(static_cast<std::uint32_t>(column)) * 0x9e3779b97f4a7c15ull ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(row)) * 0xc2b2ae3d27d4eb4full
        ^ static_cast<std::uint64_t>(level) * 0x165667b19e3779f9ull;
    hash ^= hash >> 31;
    hash *= 0xbf58476d1ce4e5b9ull;
    hash ^= hash >> 29;
    return 1 + static_cast<double>(hash >> 11) / static_cast<double>(1ull << 53);
}

// Values over the finest grid, `1 << deepest` cells across, and over each
// coarser grid halving it: each cell the most of its four (so a cell knows the
// most anywhere in it, however little of it there is) or their mean.
struct Pyramid {
    enum class Combine { most, mean };
    int deepest = 0;
    std::vector<std::vector<float>> levels;

    static Pyramid of(std::vector<float> finest, int deepest, Combine combine) {
        assert(finest.size() == (std::size_t {1} << deepest) * (std::size_t {1} << deepest));
        Pyramid pyramid;
        pyramid.deepest = deepest;
        pyramid.levels.resize(static_cast<std::size_t>(deepest) + 1);
        pyramid.levels[static_cast<std::size_t>(deepest)] = std::move(finest);
        for (auto level = deepest - 1; level >= 0; --level) {
            const auto across = std::size_t {1} << level;
            const auto& finer = pyramid.levels[static_cast<std::size_t>(level) + 1];
            auto& here = pyramid.levels[static_cast<std::size_t>(level)];
            here.assign(across * across, 0.0f);
            for (std::size_t row = 0; row < across; ++row) {
                for (std::size_t column = 0; column < across; ++column) {
                    const auto at = [&](std::size_t r, std::size_t c) { return finer[r * across * 2 + c]; };
                    const float four[] {at(row * 2, column * 2), at(row * 2, column * 2 + 1), at(row * 2 + 1, column * 2), at(row * 2 + 1, column * 2 + 1)};
                    here[row * across + column] = combine == Combine::most ? *std::max_element(std::begin(four), std::end(four)) : (four[0] + four[1] + four[2] + four[3]) * .25f;
                }
            }
        }
        return pyramid;
    }
    float at(int level, int row, int column) const { return levels[static_cast<std::size_t>(level)][static_cast<std::size_t>(row) * (std::size_t {1} << level) + static_cast<std::size_t>(column)]; }
};

// A cell of the curve: how deep it is, where in its grid, and its middle and
// width in the finest grid's cells.
struct Cell {
    int level, row, column;
    double x, y, size;
};

// The curve over the grid `1 << deepest` cells across: `visit(cell)` for each
// cell it passes through, in order, a cell divided while `divides(cell)` says
// so (and always down to `shallowest`), never past `deepest`. A visit that
// returns false stops the curve there.
template <typename Divides, typename Visit>
void curve(int deepest, Divides&& divides, Visit&& visit, int shallowest = 2) {
    bool stopped = false;
    // A cell from its corner (x, y) along i and j, as the curve by vectors goes.
    const auto walk = [&](const auto& self, double x, double y, double ix, double iy, double jx, double jy, int level) -> void {
        if (stopped) { return; }
        const auto size = std::abs(ix) + std::abs(iy);
        const auto across = 1 << level;
        const Cell cell {level, std::clamp(static_cast<int>((y + (iy + jy) / 2) / size), 0, across - 1), std::clamp(static_cast<int>((x + (ix + jx) / 2) / size), 0, across - 1),
            x + (ix + jx) / 2, y + (iy + jy) / 2, size};
        if (level < deepest && (level < shallowest || divides(cell))) {
            // Hilbert's order: the first quarter turned one way, the middle
            // two as the whole, the last turned the other.
            const auto hx = ix / 2, hy = iy / 2, kx = jx / 2, ky = jy / 2;
            self(self, x, y, kx, ky, hx, hy, level + 1);
            self(self, x + hx, y + hy, hx, hy, kx, ky, level + 1);
            self(self, x + hx + kx, y + hy + ky, hx, hy, kx, ky, level + 1);
            self(self, x + hx + jx, y + hy + jy, -kx, -ky, -hx, -hy, level + 1);
            return;
        }
        if constexpr (std::is_same_v<std::invoke_result_t<Visit&, const Cell&>, bool>) {
            stopped = !visit(cell);
        } else {
            visit(cell);
        }
    };
    const auto cells = static_cast<double>(1 << deepest);
    walk(walk, 0, 0, cells, 0, 0, cells, 0);
}

// The curve as finely divided as what each cell holds asks: a cell divides
// while its width (in finest cells) times what it holds times `gain` is more
// than `finest` (in finest cells) times its own threshold, so at full a cell
// is divided down to about `finest` across.
template <typename Visit>
void adaptive(const Pyramid& pyramid, double gain, double finest, Visit&& visit, int shallowest = 2) {
    curve(pyramid.deepest, [&](const Cell& cell) {
        const auto value = pyramid.at(cell.level, cell.row, cell.column);
        return value > 0 && cell.size * value * gain > finest * threshold(cell.column, cell.row, cell.level);
    }, visit, shallowest);
}

// As fine as asked, or less, evenly, to come to no more than `most`: the
// largest gain from 0 to 1 at which `count(gain)` fits, found by halving;
// none if even the coarsest curve (gain 0) doesn't.
template <typename Count>
std::optional<double> fitting(std::size_t most, Count&& count) {
    if (count(1.0) <= most) { return 1.0; }
    if (count(0.0) > most) { return std::nullopt; }
    double low = 0, high = 1;
    for (int halving = 0; halving < 10; ++halving) {
        const auto middle = (low + high) / 2;
        (count(middle) > most ? high : low) = middle;
    }
    return low;
}
}
