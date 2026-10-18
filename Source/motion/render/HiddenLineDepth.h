#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace motion::hidden {
// Nothing nearer an eye than this (a camera, or a light seeing what it
// shadows) is seen, drawn, or hides anything.
inline constexpr double nearPlane = .05;

// A point on screen, in output units (the frame spans -1 to 1), with its
// inverse depth (1 / distance along the view): what a depth buffer compares.
struct Screen {
    double x, y, w;
};

// How far from the middle the Scope shows, in output units: -1 to 1 across a
// square canvas, and further across the longer side of one that is not; all
// of it divided by the Scope's gain (a unit of signal spans less than half
// the canvas, so the Scope shows a little past 1).
struct Reach {
    double x = 1, y = 1;

    static Reach ofCanvas(int width, int height, double gain = 1) {
        if (width <= 0 || height <= 0 || !(gain > 0)) { return {}; }
        const auto aspect = static_cast<double>(width) / height;
        return {std::max(1.0, aspect) / gain, std::max(1.0, 1 / aspect) / gain};
    }
    bool operator==(const Reach&) const = default;
};

// What hides what, point by point: a depth buffer over what the Scope shows
// (a little past it, for the beam's width), at the same pixel size however
// wide that is, holding the nearest surface at every pixel. Surfaces are
// drawn into it as triangles small enough to follow whatever bends them; a point shows when nothing in its pixel
// is in front of it. Each pixel keeps which triangle is nearest at its centre,
// and a point is tested against that triangle's plane where the point is, not
// where the centre is: a surface seen edge on changes depth steeply across a
// pixel, and testing at the centre would need it pushed back as far, letting
// whatever is just behind it show. A point inside that triangle is tested
// against its depth there, pushed back by a small part of that depth and a
// little for surfaces bent between their corners. A point just outside it (on
// the far side of an edge two surfaces share, in a pixel whose centre the
// other has) meets its plane carried past the edge, so the plane is pushed
// back by its depth slope across as far as the point is outside, as polygon
// offset does: a line on a surface (its edges, where two surfaces touch) is
// never hidden by it. Nothing outside the buffer can be seen, so a point there
// shows.
class DepthBuffer {
public:
    // How far past what the Scope shows the buffer reaches, and its pixels:
    // 1024 across a square canvas, as a camera's buffer has them (a light's
    // shadows, seen from the light, need fewer).
    static constexpr double overscan = 1.05;
    static constexpr int pixelsAcross = 1024;
    static constexpr double pixel = 2 * overscan / pixelsAcross;
    // In squared pixels: far less than a pixel, far more than rounding.
    static constexpr double edgeSlack = 1.0e-6;
    // How near an edge, in pixels, a point is on it.
    static constexpr double onEdge = 1.0e-3;
    // How far a plane is pushed back: this much of its depth (a line this
    // close to a surface, two parts touching, is on it, however steeply the
    // surface is seen); its depth slope across this many pixels, for surfaces
    // bent between their corners; and across this many more for every pixel a
    // point is outside its triangle.
    static constexpr double depthPush = 2.0e-3;
    static constexpr double insidePush = .25;
    static constexpr double slopePush = 1.5;

    // A pixel coordinate as an int, however far off the buffer (or not a
    // number) the point is: converting one out of int's range is undefined.
    static int cell(double value) { return std::isfinite(value) ? static_cast<int>(std::clamp(value, -1.0e9, 1.0e9)) : -1000000000; }

    explicit DepthBuffer(Reach reach = {}, int across = pixelsAcross) : size(2 * overscan / std::max(16, across)) { cover(reach); }
    // Its pixels' size, in output units.
    double pixelSize() const { return size; }

    // Covers what a canvas shows, emptied; only a new reach reallocates.
    void cover(Reach reach) {
        if (reach == covered && !pixels.empty()) {
            clear();
            return;
        }
        covered = reach;
        originX = reach.x * overscan;
        originY = reach.y * overscan;
        width = static_cast<int>(std::ceil(2 * originX / size));
        height = static_cast<int>(std::ceil(2 * originY / size));
        pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Pixel {});
        empty = true;
        clear();
    }

    // Only what was drawn since the last clear is cleared.
    void clear() {
        if (!empty) {
            for (auto y = drawnBottom; y <= drawnTop; ++y) {
                const auto row = pixels.begin() + static_cast<std::ptrdiff_t>(y) * width;
                std::fill(row + drawnLeft, row + drawnRight + 1, Pixel {});
            }
        }
        drawnLeft = width;
        drawnRight = -1;
        drawnBottom = height;
        drawnTop = -1;
        empty = true;
        planes.clear();
    }
    bool isEmpty() const { return empty; }

    // Draws a triangle of surface; corners must be in front of the eye.
    void triangle(Screen a, Screen b, Screen c) {
        const auto ax = toPixelX(a.x), ay = toPixelY(a.y), bx = toPixelX(b.x), by = toPixelY(b.y), cx = toPixelX(c.x), cy = toPixelY(c.y);
        const auto area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (!std::isfinite(area) || std::abs(area) < 1.0e-12) { return; }
        // The pixels whose centres (x + .5) the triangle's box can reach, with
        // room for a centre right on an edge.
        constexpr double margin = 1.0e-6;
        const auto left = std::max(0, cell(std::ceil(std::min({ax, bx, cx}) - .5 - margin)));
        const auto right = std::min(width - 1, cell(std::floor(std::max({ax, bx, cx}) - .5 + margin)));
        const auto bottom = std::max(0, cell(std::ceil(std::min({ay, by, cy}) - .5 - margin)));
        const auto top = std::min(height - 1, cell(std::floor(std::max({ay, by, cy}) - .5 + margin)));
        if (left > right || bottom > top) { return; }
        // Inverse depth is linear on screen: its plane, and how steeply it
        // changes across a pixel, which is how far it is pushed back.
        const auto inverseArea = 1 / area;
        const auto dwdx = ((b.w - a.w) * (cy - ay) - (c.w - a.w) * (by - ay)) * inverseArea;
        const auto dwdy = ((c.w - a.w) * (bx - ax) - (b.w - a.w) * (cx - ax)) * inverseArea;
        const auto slope = std::abs(dwdx) + std::abs(dwdy);
        const auto push = insidePush * slope;
        const auto sign = area > 0 ? 1.0 : -1.0;
        // Each edge's test is linear across the pixels, as is depth. A row of
        // a wider triangle visits only the span its edges allow (a pixel wider
        // each side, as rounding may land there); a narrow one's rows are too
        // short for finding spans to pay. Every pixel visited is tested exactly.
        const Edge edges[3] {Edge::of(ax, ay, bx, by, sign), Edge::of(bx, by, cx, cy, sign), Edge::of(cx, cy, ax, ay, sign)};
        const auto index = static_cast<std::int32_t>(planes.size());
        planes.push_back({a.w - dwdx * ax - dwdy * ay - push, dwdx, dwdy, slopePush * slope, {edges[0].normalised(), edges[1].normalised(), edges[2].normalised()}, nextTag});
        const auto narrow = right - left < 6;
        const auto spans = narrow ? std::optional<Spans>() : std::optional<Spans>(Spans(edges));
        for (auto y = bottom; y <= top; ++y) {
            const auto py = y + .5;
            const auto [first, last] = narrow ? std::pair {left, right} : spans->at(py, left, right);
            if (first > last) { continue; }
            const auto firstX = first + .5;
            // Inside, edges included (with room for rounding, which can put a
            // pixel on a shared edge just outside both neighbours): each edge
            // allows a run of the row's pixels, found once, so the pixels
            // between are only compared in depth.
            auto from = 0.0, to = static_cast<double>(last - first);
            for (const auto& edge : edges) {
                const auto room = edge.at(firstX, py) + edgeSlack;
                if (edge.stepX > 0) {
                    from = std::max(from, std::ceil(-room / edge.stepX));
                } else if (edge.stepX < 0) {
                    to = std::min(to, std::floor(-room / edge.stepX));
                } else if (room < 0) {
                    to = -1;
                }
            }
            if (from > to) { continue; }
            const auto start = static_cast<int>(from), end = static_cast<int>(to);
            const auto w = a.w + dwdx * (firstX - ax) + dwdy * (py - ay);
            auto* cell = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(first);
            for (auto step = start; step <= end; ++step) {
                const auto here = static_cast<float>(w + dwdx * step);
                if (here > cell[step].w) { cell[step] = {here, index}; }
            }
        }
        drawnLeft = std::min(drawnLeft, left);
        drawnRight = std::max(drawnRight, right);
        drawnBottom = std::min(drawnBottom, bottom);
        drawnTop = std::max(drawnTop, top);
        empty = false;
    }

    // What the triangles drawn next are of (a surface and its face, say), so
    // what is nearest at a point can be asked for.
    void tagNext(std::uint64_t tag) { nextTag = tag; }
    // What is nearest at a point of the canvas, by its tag, and its inverse
    // depth there; none where nothing is drawn.
    struct Nearest {
        std::uint64_t tag;
        double w;
    };
    std::optional<Nearest> nearestAt(double x, double y) const {
        if (empty) { return std::nullopt; }
        const auto px = toPixelX(x), py = toPixelY(y);
        const auto column = cell(std::floor(px)), row = cell(std::floor(py));
        if (column < 0 || row < 0 || column >= width || row >= height) { return std::nullopt; }
        const auto& here = pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)];
        if (here.w == 0) { return std::nullopt; }
        const auto& plane = planes[static_cast<std::size_t>(here.owner)];
        return Nearest {plane.tag, plane.at(px, py)};
    }

    // Whether a point shows: nothing drawn in front of it in its pixel.
    bool shows(Screen point) const { return !hiddenBy(point).has_value(); }
    // What hides a point, by its tag (see tagNext): none where it shows.
    std::optional<std::uint64_t> hiddenBy(Screen point) const {
        if (empty) { return std::nullopt; }
        const auto px = toPixelX(point.x), py = toPixelY(point.y);
        const auto x = cell(std::floor(px)), y = cell(std::floor(py));
        if (x < 0 || y < 0 || x >= width || y >= height) { return std::nullopt; }
        const auto cell = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const auto& here = pixels[cell];
        if (here.w == 0) { return std::nullopt; }
        const auto& plane = planes[static_cast<std::size_t>(here.owner)];
        const auto outside = plane.outside(px, py);
        // Inside the triangle nearest at the pixel's centre: its depth there.
        if (outside <= 0) { return inFront(point, plane.at(px, py)) ? std::nullopt : std::optional(plane.tag); }
        // Past its edge (a pixel its edge crosses): any triangle nearest at a
        // pixel around this one that covers the point decides. A triangle
        // seen edge on can have the centre of a pixel its edge just crosses;
        // carried past that edge, its steep plane falls away, and whatever
        // covers the point instead would be lost.
        bool covered = false;
        for (auto row = std::max(0, y - 1); row <= std::min(height - 1, y + 1); ++row) {
            for (auto column = std::max(0, x - 1); column <= std::min(width - 1, x + 1); ++column) {
                const auto& nearby = pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)];
                if (nearby.w == 0 || nearby.owner == here.owner) { continue; }
                const auto& other = planes[static_cast<std::size_t>(nearby.owner)];
                if (other.outside(px, py) > 0) { continue; }
                covered = true;
                if (!inFront(point, other.at(px, py))) { return other.tag; }
            }
        }
        if (covered) { return std::nullopt; }
        // None does: the nearest triangle carried past its edge, pushed back
        // by its slope across as far as the point is outside.
        return inFront(point, plane.at(px, py) - plane.slope * outside) ? std::nullopt : std::optional(plane.tag);
    }

    // How much of a point the surfaces round it leave in view, from 0 to 1:
    // each pixel within a pixel and a half asks whether its nearest surface,
    // carried to where the point is, is in front of it, weighted by how near
    // its centre is. The share changes smoothly as the point or what hides it
    // moves, rather than a whole pixel at a time, so a light's shadows keep
    // still edges from frame to frame. A surface the point is on is never in
    // front of it there, so none shadows itself.
    double shownShare(Screen point) const {
        if (empty) { return 1; }
        constexpr double reach = 1.5;
        const auto px = toPixelX(point.x), py = toPixelY(point.y);
        const auto cx = px - .5, cy = py - .5;
        const auto x0 = cell(std::floor(cx - reach)) + 1, y0 = cell(std::floor(cy - reach)) + 1;
        double shown = 0, total = 0;
        for (auto y = y0; y < y0 + 3; ++y) {
            const auto wy = reach - std::abs(y - cy);
            if (!(wy > 0)) { continue; }
            for (auto x = x0; x < x0 + 3; ++x) {
                const auto wx = reach - std::abs(x - cx);
                if (!(wx > 0)) { continue; }
                const auto weight = wx * wy;
                total += weight;
                if (x < 0 || y < 0 || x >= width || y >= height) {
                    shown += weight;
                    continue;
                }
                const auto& here = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                if (here.w == 0 || inFront(point, planes[static_cast<std::size_t>(here.owner)].at(px, py))) { shown += weight; }
            }
        }
        return total > 0 ? shown / total : 1;
    }

private:
    // Whether a point is in front of a surface at `w` there, or on it: within
    // a small part of its own depth, so a surface reaching far nearer the eye
    // elsewhere is not pushed back further for it.
    static bool inFront(Screen point, double w) { return point.w * (1 + depthPush) >= w; }
    double toPixelX(double value) const { return (value + originX) * (1 / size); }
    double toPixelY(double value) const { return (value + originY) * (1 / size); }
    // Which side of the edge from (x0, y0) to (x1, y1) a pixel centre is on:
    // positive inside, as a plane over the pixels.
    struct Edge {
        double stepX, stepY, offset;
        static Edge of(double x0, double y0, double x1, double y1, double sign) {
            return {-sign * (y1 - y0), sign * (x1 - x0), sign * ((y1 - y0) * x0 - (x1 - x0) * y0)};
        }
        double at(double x, double y) const { return stepX * x + stepY * y + offset; }
        // Scaled so that it is the distance from the edge, in pixels.
        Edge normalised() const {
            const auto length = std::hypot(stepX, stepY);
            return length > 0 ? Edge {stepX / length, stepY / length, offset / length} : *this;
        }
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

    // A triangle's plane of inverse depth over the pixels, pushed back, and
    // its edges, to push it back further past them.
    struct Plane {
        double w, dwdx, dwdy, slope;
        Edge edges[3];
        std::uint64_t tag = 0;
        double at(double x, double y) const { return w + dwdx * x + dwdy * y; }
        // How many pixels outside the triangle a point is: none for a point
        // on an edge to well within a pixel (a line drawn along it).
        double outside(double x, double y) const {
            const auto distance = -std::min({edges[0].at(x, y), edges[1].at(x, y), edges[2].at(x, y)});
            return distance > onEdge ? distance : 0.0;
        }
    };

    // The inverse depth nearest at a pixel's centre, and whose it is.
    struct Pixel {
        float w = 0;
        std::int32_t owner = 0;
    };
    double size = pixel;
    Reach covered;
    double originX = 0, originY = 0;
    int width = 0, height = 0;
    std::vector<Pixel> pixels;
    std::vector<Plane> planes;
    std::uint64_t nextTag = 0;
    // The pixels drawn since the last clear.
    int drawnLeft = 0, drawnRight = -1, drawnBottom = 0, drawnTop = -1;
    bool empty = true;
};
}
