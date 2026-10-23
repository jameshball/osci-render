#pragma once

#include "../model/Vec3.h"
#include <array>
#include <cmath>
#include <optional>

// Points on a triangle: by the weights of its corners, so whoever has them
// can put them wherever the corners go, and on an even grid across it. Pure
// geometry, so it can be tested alone.
namespace motion::hidden {
// A point on a triangle, as the weights of its corners.
using Weights = std::array<double, 3>;

// The point at `weights` on the triangle with corners a, b and c.
inline Vec3 weighted(const Vec3& a, const Vec3& b, const Vec3& c, const Weights& weights) { return a * weights[0] + b * weights[1] + c * weights[2]; }
inline Vec3 weighted(const std::array<Vec3, 3>& corners, const Weights& weights) { return weighted(corners[0], corners[1], corners[2], weights); }

// The weights `share` of the way from `from` to `to`.
inline Weights mixed(const Weights& from, const Weights& to, double share) {
    Weights weights;
    for (std::size_t corner = 0; corner < 3; ++corner) { weights[corner] = from[corner] + (to[corner] - from[corner]) * share; }
    return weights;
}

// How far a point on the plane of the triangle with corners a, b and c is
// toward b and toward c, from a: the weights of those two corners. None for
// a triangle with no more area than `least` (as the square of twice its area).
inline std::optional<std::array<double, 2>> sharesOn(Vec3 a, Vec3 b, Vec3 c, Vec3 point, double least) {
    const auto u = b - a, v = c - a, w = point - a;
    const auto uu = u.dot(u), uv = u.dot(v), vv = v.dot(v), wu = w.dot(u), wv = w.dot(v);
    const auto area = uu * vv - uv * uv;
    if (!(std::abs(area) > least)) { return std::nullopt; }
    return std::array<double, 2> {(vv * wu - uv * wv) / area, (uu * wv - uv * wu) / area};
}

// A point's weights on the corners of a triangle, as it lies on its plane:
// all on its first corner for a triangle with no area.
inline Weights weightsOn(Vec3 a, Vec3 b, Vec3 c, Vec3 point) {
    const auto shares = sharesOn(a, b, c, point, 0);
    if (!shares.has_value()) { return {1, 0, 0}; }
    const auto [second, third] = *shares;
    return {1 - second - third, second, third};
}

// Where a point of a triangle cut evenly into a grid `cuts` across is kept,
// row by row from its first corner: row r holds cuts + 1 - r points.
inline int gridIndex(int cuts, int row, int column) { return row * (cuts + 1) - row * (row - 1) / 2 + column; }
}
