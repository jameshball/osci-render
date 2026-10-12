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
// A surface whose faces count only from further than `within` of either end
// of a line: a line along where it turns away grazes its faces near the line.
struct GrazedSurface {
    std::uint32_t surface = std::numeric_limits<std::uint32_t>::max();
    double within = 0;
};

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
        fill(0, 0, static_cast<std::uint32_t>(triangles.size()), 0);
    }
    bool empty() const { return triangles.empty(); }

    // Whether a face is between `from` and `to`, other than a face of
    // `ignored`. Hits within `nearFrom` of `from` or `nearTo` of `to` (in the
    // scene's units) don't count: a point on a surface is never hidden by it.
    bool blocked(Vec3 from, Vec3 to, double nearFrom, double nearTo, std::uint32_t ignored = none, GrazedSurface near = {}) const {
        if (nodes.empty()) { return false; }
        const auto span = to - from;
        const auto length = span.length();
        if (!(length > nearFrom + nearTo)) { return false; }
        const auto first = nearFrom / length, last = 1 - nearTo / length;
        const auto nearFirst = near.within / length, nearLast = 1 - near.within / length;
        const Vec3 inverse {1 / span.x, 1 / span.y, 1 / span.z};
        // Deep enough for any tree fill() builds (see deepest).
        std::array<std::uint32_t, deepest + 34> stack {};
        std::size_t depth = 0;
        stack[depth++] = 0;
        while (depth > 0) {
            const auto& node = nodes[stack[--depth]];
            if (!crosses(node, from, inverse, first, last)) { continue; }
            if (node.count > 0) {
                for (auto index = node.start; index < node.start + node.count; ++index) {
                    const auto& face = triangles[order[index]];
                    if (face.surface == ignored) { continue; }
                    const auto along = hit(face, from, span);
                    if (!(along > first && along < last)) { continue; }
                    if (face.surface != near.surface || (along > nearFirst && along < nearLast)) { return true; }
                }
                continue;
            }
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
    // Below this depth nodes are halved at their middle triangle, so no
    // branch is more than 32 deeper (a node holds under 2^32 triangles).
    static constexpr std::size_t deepest = 48;

    // Fills the node at `at`, `level` deep, with the triangles from `begin`
    // to `end` in `order`, splitting them while there are more than a leaf
    // holds.
    void fill(std::uint32_t at, std::uint32_t begin, std::uint32_t end, std::size_t level) {
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
        // Split where the two halves' boxes would be least often crossed:
        // by their surface areas times how many triangles each holds,
        // trying a few cuts along the longest side of the centres' box.
        constexpr int bins = 12;
        struct Bin {
            Vec3 low {largest, largest, largest}, high {-largest, -largest, -largest};
            std::uint32_t count = 0;
        };
        std::array<Bin, bins> binned {};
        const auto binOf = [&](std::uint32_t triangle) {
            return std::min(bins - 1, static_cast<int>((centres[triangle][axis] - centreLow[axis]) / extent[axis] * bins));
        };
        for (auto index = begin; index < end; ++index) {
            const auto& face = triangles[order[index]];
            auto& bin = binned[static_cast<std::size_t>(binOf(order[index]))];
            ++bin.count;
            for (const auto& corner : {face.a, face.b, face.c}) {
                bin.low = {std::min(bin.low.x, corner.x), std::min(bin.low.y, corner.y), std::min(bin.low.z, corner.z)};
                bin.high = {std::max(bin.high.x, corner.x), std::max(bin.high.y, corner.y), std::max(bin.high.z, corner.z)};
            }
        }
        const auto area = [](Vec3 a, Vec3 b) {
            const auto size = b - a;
            return size.x < 0 ? 0.0 : size.x * size.y + size.y * size.z + size.z * size.x;
        };
        double best = largest;
        int cut = bins / 2;
        for (int split = 1; split < bins; ++split) {
            Bin left, right;
            for (int index = 0; index < bins; ++index) {
                auto& side = index < split ? left : right;
                const auto& bin = binned[static_cast<std::size_t>(index)];
                if (bin.count == 0) { continue; }
                side.count += bin.count;
                side.low = {std::min(side.low.x, bin.low.x), std::min(side.low.y, bin.low.y), std::min(side.low.z, bin.low.z)};
                side.high = {std::max(side.high.x, bin.high.x), std::max(side.high.y, bin.high.y), std::max(side.high.z, bin.high.z)};
            }
            if (left.count == 0 || right.count == 0) { continue; }
            const auto cost = area(left.low, left.high) * left.count + area(right.low, right.high) * right.count;
            if (cost < best) {
                best = cost;
                cut = split;
            }
        }
        auto middle = static_cast<std::uint32_t>(std::partition(order.begin() + begin, order.begin() + end, [&](std::uint32_t triangle) { return binOf(triangle) < cut; }) - order.begin());
        // Every centre in one bin, or deep already: halved at the middle triangle instead.
        if (middle == begin || middle == end || level >= deepest) {
            middle = begin + (end - begin) / 2;
            std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
                [&](std::uint32_t one, std::uint32_t other) { return centres[one][axis] < centres[other][axis]; });
        }
        const auto children = static_cast<std::uint32_t>(nodes.size());
        nodes[at].start = children;
        nodes[at].count = 0;
        nodes.emplace_back();
        nodes.emplace_back();
        fill(children, begin, middle, level + 1);
        fill(children + 1, middle, end, level + 1);
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
    // Where the segment's line meets a triangle, as a share of the segment;
    // NaN if it doesn't (Möller and Trumbore).
    static double hit(const Triangle& face, Vec3 from, Vec3 span) {
        constexpr auto missed = std::numeric_limits<double>::quiet_NaN();
        const auto ab = face.b - face.a, ac = face.c - face.a;
        const auto across = span.cross(ac);
        const auto determinant = ab.dot(across);
        if (std::abs(determinant) < 1.0e-18) { return missed; }
        const auto inverse = 1 / determinant;
        const auto offset = from - face.a;
        const auto u = offset.dot(across) * inverse;
        if (u < 0 || u > 1) { return missed; }
        const auto up = offset.cross(ab);
        const auto v = span.dot(up) * inverse;
        if (v < 0 || u + v > 1) { return missed; }
        return ac.dot(up) * inverse;
    }

    std::vector<Triangle> triangles;
    std::vector<Vec3> centres;
    std::vector<std::uint32_t> order;
    std::vector<Node> nodes;
};
}
