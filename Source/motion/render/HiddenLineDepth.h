#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace motion::hidden {
// A point on screen, in output units (the frame spans -1 to 1), with its
// inverse depth (1 / distance along the view): what a depth buffer compares.
struct Screen {
    double x, y, w;
};

// What hides what, point by point: a depth buffer over the frame, holding the
// nearest surface at every pixel. Surfaces are drawn into it as triangles small
// enough to follow whatever bends them; a point shows when nothing in its pixel
// is in front of it. Each triangle is drawn pushed back by its own depth slope
// across a pixel or two, as polygon offset does, so a line on a surface (its
// edges, where two surfaces touch) is never hidden by it.
class DepthBuffer {
public:
    // Pixels across the frame, and how far past its edges the buffer reaches.
    static constexpr int size = 1024;
    static constexpr double extent = 1.05;
    static constexpr double pixel = 2 * extent / size;
    // In squared pixels: far less than a pixel, far more than rounding.
    static constexpr double edgeSlack = 1.0e-6;

    DepthBuffer() : nearest(static_cast<std::size_t>(size) * size, 0.0f) {}

    // Only what was drawn since the last clear is cleared.
    void clear() {
        if (!empty) {
            for (auto y = drawnBottom; y <= drawnTop; ++y) {
                const auto row = nearest.begin() + static_cast<std::ptrdiff_t>(y) * size;
                std::fill(row + drawnLeft, row + drawnRight + 1, 0.0f);
            }
        }
        drawnLeft = size;
        drawnRight = -1;
        drawnBottom = size;
        drawnTop = -1;
        empty = true;
    }
    bool isEmpty() const { return empty; }

    // Draws a triangle of surface; corners must be in front of the eye.
    void triangle(Screen a, Screen b, Screen c) {
        const auto ax = toPixel(a.x), ay = toPixel(a.y), bx = toPixel(b.x), by = toPixel(b.y), cx = toPixel(c.x), cy = toPixel(c.y);
        const auto area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (!std::isfinite(area) || std::abs(area) < 1.0e-12) { return; }
        const auto left = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const auto right = std::min(size - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const auto bottom = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const auto top = std::min(size - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        if (left > right || bottom > top) { return; }
        // Inverse depth is linear on screen: its plane, and how steeply it
        // changes across a pixel, which is how far it is pushed back.
        const auto inverseArea = 1 / area;
        const auto dwdx = ((b.w - a.w) * (cy - ay) - (c.w - a.w) * (by - ay)) * inverseArea;
        const auto dwdy = ((c.w - a.w) * (bx - ax) - (b.w - a.w) * (cx - ax)) * inverseArea;
        const auto push = 1.5 * (std::abs(dwdx) + std::abs(dwdy)) + 1.0e-4 * std::max({a.w, b.w, c.w});
        const auto sign = area > 0 ? 1.0 : -1.0;
        // Each edge's test is linear across the pixels, as is depth. A row of
        // a wider triangle visits only the span its edges allow (a pixel wider
        // each side, as rounding may land there); a narrow one's rows are too
        // short for finding spans to pay. Every pixel visited is tested exactly.
        const Edge edges[3] {Edge::of(ax, ay, bx, by, sign), Edge::of(bx, by, cx, cy, sign), Edge::of(cx, cy, ax, ay, sign)};
        const auto narrow = right - left < 6;
        const auto spans = narrow ? std::optional<Spans>() : std::optional<Spans>(Spans(edges));
        for (auto y = bottom; y <= top; ++y) {
            const auto py = y + .5;
            const auto [first, last] = narrow ? std::pair {left, right} : spans->at(py, left, right);
            if (first > last) { continue; }
            const auto firstX = first + .5;
            // Inside, edges included (with room for rounding, which can put a
            // pixel on a shared edge just outside both neighbours).
            auto e0 = edges[0].at(firstX, py), e1 = edges[1].at(firstX, py), e2 = edges[2].at(firstX, py);
            auto w = a.w + dwdx * (firstX - ax) + dwdy * (py - ay) - push;
            auto* cell = nearest.data() + static_cast<std::size_t>(y) * size + static_cast<std::size_t>(first);
            for (auto x = first; x <= last; ++x, ++cell) {
                if (e0 >= -edgeSlack && e1 >= -edgeSlack && e2 >= -edgeSlack && w > *cell) { *cell = static_cast<float>(w); }
                e0 += edges[0].stepX;
                e1 += edges[1].stepX;
                e2 += edges[2].stepX;
                w += dwdx;
            }
        }
        drawnLeft = std::min(drawnLeft, left);
        drawnRight = std::max(drawnRight, right);
        drawnBottom = std::min(drawnBottom, bottom);
        drawnTop = std::max(drawnTop, top);
        empty = false;
    }

    // Whether a point shows: nothing drawn in front of it in its pixel.
    bool shows(Screen point) const {
        if (empty) { return true; }
        const auto x = static_cast<int>(std::floor(toPixel(point.x))), y = static_cast<int>(std::floor(toPixel(point.y)));
        if (x < 0 || y < 0 || x >= size || y >= size) { return true; }
        return point.w >= nearest[static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)];
    }

private:
    static double toPixel(double value) { return (value + extent) * (1 / pixel); }
    // Which side of the edge from (x0, y0) to (x1, y1) a pixel centre is on:
    // positive inside, as a plane over the pixels.
    struct Edge {
        double stepX, stepY, offset;
        static Edge of(double x0, double y0, double x1, double y1, double sign) {
            return {-sign * (y1 - y0), sign * (x1 - x0), sign * ((y1 - y0) * x0 - (x1 - x0) * y0)};
        }
        double at(double x, double y) const { return stepX * x + stepY * y + offset; }
    };
    // The pixels of a row that can be inside all three edges. Where an edge
    // allows a pixel, stepX * (x + .5) + stepY * py + offset >= -edgeSlack,
    // is a bound on x that moves linearly with the row: each edge's bound is
    // found once, so a row's span costs no division.
    class Spans {
    public:
        explicit Spans(const Edge (&edges)[3]) {
            for (const auto& edge : edges) {
                if (edge.stepX == 0) {
                    flat[flatCount++] = &edge;
                    continue;
                }
                auto& bound = edge.stepX > 0 ? lower[lowerCount++] : upper[upperCount++];
                bound = {-edge.stepY / edge.stepX, -(edge.offset + edgeSlack) / edge.stepX - .5};
            }
        }
        std::pair<int, int> at(double py, int left, int right) const {
            auto low = static_cast<double>(left), high = static_cast<double>(right);
            for (int index = 0; index < lowerCount; ++index) { low = std::max(low, lower[index].slope * py + lower[index].offset); }
            for (int index = 0; index < upperCount; ++index) { high = std::min(high, upper[index].slope * py + upper[index].offset); }
            for (int index = 0; index < flatCount; ++index) {
                if (flat[index]->stepY * py + flat[index]->offset + edgeSlack < 0) { return {1, 0}; }
            }
            if (!(low <= high + 2)) { return {1, 0}; }
            return {std::max(left, static_cast<int>(std::floor(low)) - 1), std::min(right, static_cast<int>(std::ceil(high)) + 1)};
        }

    private:
        struct Bound { double slope, offset; };
        Bound lower[3] {}, upper[3] {};
        const Edge* flat[3] {};
        int lowerCount = 0, upperCount = 0, flatCount = 0;
    };

    std::vector<float> nearest;
    // The pixels drawn since the last clear.
    int drawnLeft = size, drawnRight = -1, drawnBottom = size, drawnTop = -1;
    bool empty = true;
};
}
