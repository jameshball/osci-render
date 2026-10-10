#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
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

    void clear() {
        std::fill(nearest.begin(), nearest.end(), 0.0f);
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
        const auto dwdx = ((b.w - a.w) * (cy - ay) - (c.w - a.w) * (by - ay)) / area;
        const auto dwdy = ((c.w - a.w) * (bx - ax) - (b.w - a.w) * (cx - ax)) / area;
        const auto push = 1.5 * (std::abs(dwdx) + std::abs(dwdy)) + 1.0e-4 * std::max({a.w, b.w, c.w});
        const auto sign = area > 0 ? 1.0 : -1.0;
        for (auto y = bottom; y <= top; ++y) {
            const auto py = y + .5;
            for (auto x = left; x <= right; ++x) {
                const auto px = x + .5;
                // Inside, edges included (with room for rounding, which can
                // put a pixel on a shared edge just outside both neighbours).
                const auto e0 = sign * ((bx - ax) * (py - ay) - (by - ay) * (px - ax));
                const auto e1 = sign * ((cx - bx) * (py - by) - (cy - by) * (px - bx));
                const auto e2 = sign * ((ax - cx) * (py - cy) - (ay - cy) * (px - cx));
                if (e0 < -edgeSlack || e1 < -edgeSlack || e2 < -edgeSlack) { continue; }
                const auto w = a.w + dwdx * (px - ax) + dwdy * (py - ay) - push;
                auto& cell = nearest[static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)];
                if (w > cell) { cell = static_cast<float>(w); }
            }
        }
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
    static double toPixel(double value) { return (value + extent) / pixel; }

    std::vector<float> nearest;
    bool empty = true;
};
}
