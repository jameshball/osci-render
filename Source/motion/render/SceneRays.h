#pragma once

#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Whether anything in the scene is between two points: its surfaces' faces,
// as triangles in a tree of boxes round them, so a test visits only the few
// it could hit. Pure geometry, so it can be tested alone.
namespace motion::hidden {
class SceneRays {
public:
    struct Triangle {
        Vec3 a, b, c;
        // The surface it is a face of.
        std::uint32_t surface;
    };

    // Takes the scene's faces, replacing any it had.
    void build(std::vector<Triangle> faces) {
        triangles = std::move(faces);
        nodes.clear();
        order.resize(triangles.size());
        for (std::uint32_t index = 0; index < order.size(); ++index) { order[index] = index; }
        centres.resize(triangles.size());
        for (std::size_t index = 0; index < triangles.size(); ++index) {
            const auto& face = triangles[index];
            centres[index] = (face.a + face.b + face.c) / 3.0;
        }
        if (triangles.empty()) { return; }
        nodes.emplace_back();
        fill(0, 0, static_cast<std::uint32_t>(triangles.size()));
    }
    bool empty() const { return triangles.empty(); }

    // Whether a face is between `from` and `to`, other than a face of
    // `ignored`. Hits within `nearFrom` of `from` or `nearTo` of `to` (in the
    // scene's units) don't count: a point on a surface is never hidden by it.
    bool blocked(Vec3 from, Vec3 to, double nearFrom, double nearTo, std::uint32_t ignored = none) const {
        if (nodes.empty()) { return false; }
        const auto span = to - from;
        const auto length = span.length();
        if (!(length > nearFrom + nearTo)) { return false; }
        const auto first = nearFrom / length, last = 1 - nearTo / length;
        const Vec3 inverse {1 / span.x, 1 / span.y, 1 / span.z};
        std::array<std::uint32_t, 64> stack {};
        std::size_t depth = 0;
        stack[depth++] = 0;
        while (depth > 0) {
            const auto& node = nodes[stack[--depth]];
            if (!crosses(node, from, inverse, first, last)) { continue; }
            if (node.count > 0) {
                for (auto index = node.start; index < node.start + node.count; ++index) {
                    const auto& face = triangles[order[index]];
                    if (face.surface != ignored && hits(face, from, span, first, last)) { return true; }
                }
                continue;
            }
            if (depth + 2 > stack.size()) { return false; }
            stack[depth++] = node.start;
            stack[depth++] = node.start + 1;
        }
        return false;
    }

    static constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();

private:
    // A box round its triangles; a leaf holds `count` of them from `start` in
    // `order`, a branch its two halves at `start` and the one after.
    struct Node {
        Vec3 low, high;
        std::uint32_t start = 0, count = 0;
    };
    static constexpr std::uint32_t leafSize = 4;

    // Fills the node at `at` with the triangles from `begin` to `end` in
    // `order`, splitting them while there are more than a leaf holds.
    void fill(std::uint32_t at, std::uint32_t begin, std::uint32_t end) {
        constexpr auto largest = std::numeric_limits<double>::max();
        Vec3 low {largest, largest, largest}, high {-largest, -largest, -largest};
        Vec3 centreLow = low, centreHigh = high;
        for (auto index = begin; index < end; ++index) {
            const auto& face = triangles[order[index]];
            for (const auto& corner : {face.a, face.b, face.c}) {
                low = {std::min(low.x, corner.x), std::min(low.y, corner.y), std::min(low.z, corner.z)};
                high = {std::max(high.x, corner.x), std::max(high.y, corner.y), std::max(high.z, corner.z)};
            }
            const auto& centre = centres[order[index]];
            centreLow = {std::min(centreLow.x, centre.x), std::min(centreLow.y, centre.y), std::min(centreLow.z, centre.z)};
            centreHigh = {std::max(centreHigh.x, centre.x), std::max(centreHigh.y, centre.y), std::max(centreHigh.z, centre.z)};
        }
        nodes[at].low = low;
        nodes[at].high = high;
        const auto extent = centreHigh - centreLow;
        const std::size_t axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
        if (end - begin <= leafSize || !(extent[axis] > 0)) {
            nodes[at].start = begin;
            nodes[at].count = end - begin;
            return;
        }
        // Halved at the middle triangle along the longest side of their
        // centres' box, the halves side by side.
        const auto middle = begin + (end - begin) / 2;
        std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
            [&](std::uint32_t one, std::uint32_t other) { return centres[one][axis] < centres[other][axis]; });
        const auto children = static_cast<std::uint32_t>(nodes.size());
        nodes[at].start = children;
        nodes[at].count = 0;
        nodes.emplace_back();
        nodes.emplace_back();
        fill(children, begin, middle);
        fill(children + 1, middle, end);
    }

    // Whether the part of the segment between `first` and `last` (as shares
    // of it) passes through a node's box.
    static bool crosses(const Node& node, Vec3 from, Vec3 inverse, double first, double last) {
        auto enter = first, leave = last;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto low = (node.low[axis] - from[axis]) * inverse[axis];
            const auto high = (node.high[axis] - from[axis]) * inverse[axis];
            if (std::isnan(low) || std::isnan(high)) {
                // Running along the box's face: inside only if between its sides.
                if (from[axis] < node.low[axis] || from[axis] > node.high[axis]) { return false; }
                continue;
            }
            enter = std::max(enter, std::min(low, high));
            leave = std::min(leave, std::max(low, high));
            if (enter > leave) { return false; }
        }
        return true;
    }
    // Whether the segment between those shares of it meets a triangle
    // (Möller and Trumbore).
    static bool hits(const Triangle& face, Vec3 from, Vec3 span, double first, double last) {
        const auto ab = face.b - face.a, ac = face.c - face.a;
        const auto across = span.cross(ac);
        const auto determinant = ab.dot(across);
        if (std::abs(determinant) < 1.0e-18) { return false; }
        const auto inverse = 1 / determinant;
        const auto offset = from - face.a;
        const auto u = offset.dot(across) * inverse;
        if (u < 0 || u > 1) { return false; }
        const auto up = offset.cross(ab);
        const auto v = span.dot(up) * inverse;
        if (v < 0 || u + v > 1) { return false; }
        const auto along = ac.dot(up) * inverse;
        return along > first && along < last;
    }

    std::vector<Triangle> triangles;
    std::vector<Vec3> centres;
    std::vector<std::uint32_t> order;
    std::vector<Node> nodes;
};
}
