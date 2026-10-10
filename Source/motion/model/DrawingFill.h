#pragma once

#include "PreparedDrawing.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <vector>

namespace motion::fill {
namespace detail {
// A region (an outline and its holes, none crossing) as trapezoids, two
// triangles each: cut at every vertex's height, each slab is filled between
// the edges crossing it, taken in pairs from the left.
inline void slabs(const std::vector<const Clipper2Lib::PathD*>& rings, PreparedDrawing::Mesh& mesh) {
    struct Edge { double x0, y0, x1, y1; };
    std::vector<Edge> edges;
    std::vector<double> heights;
    for (const auto* ring : rings) {
        for (std::size_t index = 0; index < ring->size(); ++index) {
            const auto& a = (*ring)[index];
            const auto& b = (*ring)[(index + 1) % ring->size()];
            heights.push_back(a.y);
            if (a.y == b.y) { continue; }
            edges.push_back(a.y < b.y ? Edge {a.x, a.y, b.x, b.y} : Edge {b.x, b.y, a.x, a.y});
        }
    }
    std::sort(heights.begin(), heights.end());
    heights.erase(std::unique(heights.begin(), heights.end()), heights.end());
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.y0 < b.y0; });
    std::vector<const Edge*> active;
    std::vector<std::pair<double, double>> crossings;
    std::size_t next = 0;
    const auto xAt = [](const Edge& edge, double y) { return edge.x0 + (edge.x1 - edge.x0) * (y - edge.y0) / (edge.y1 - edge.y0); };
    const auto vertex = [&](double x, double y) {
        mesh.vertices.emplace_back(static_cast<float>(x), static_cast<float>(y), 0.0f);
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    };
    for (std::size_t slab = 0; slab + 1 < heights.size(); ++slab) {
        const auto low = heights[slab], high = heights[slab + 1];
        while (next < edges.size() && edges[next].y0 <= low) { active.push_back(&edges[next++]); }
        std::erase_if(active, [low](const Edge* edge) { return edge->y1 <= low; });
        crossings.clear();
        for (const auto* edge : active) {
            if (edge->y0 <= low && edge->y1 >= high) { crossings.emplace_back(xAt(*edge, low), xAt(*edge, high)); }
        }
        std::sort(crossings.begin(), crossings.end(), [](const auto& a, const auto& b) { return a.first + a.second < b.first + b.second; });
        for (std::size_t pair = 0; pair + 1 < crossings.size(); pair += 2) {
            const auto& left = crossings[pair];
            const auto& right = crossings[pair + 1];
            const auto a = vertex(left.first, low), b = vertex(right.first, low), c = vertex(right.second, high), d = vertex(left.second, high);
            if (right.first > left.first) { mesh.triangles.push_back({a, b, c}); }
            if (right.second > left.second) { mesh.triangles.push_back({a, c, d}); }
        }
    }
}
}

// The area a flat drawing's closed outlines enclose (non-zero, as SVG and
// text fill), as triangles: what it hides behind it. Open strokes enclose
// nothing.
inline PreparedDrawing::Mesh of(const PreparedDrawing& drawing) {
    PreparedDrawing::Mesh mesh;
    mesh.planar = true;
    Clipper2Lib::PathsD outlines;
    Clipper2Lib::PathD current;
    osci::Point first, last;
    const auto near = [](const osci::Point& a, const osci::Point& b) { return std::abs(a.x - b.x) < 1.0e-4f && std::abs(a.y - b.y) < 1.0e-4f; };
    const auto finish = [&] {
        // Closed, with an area: back where it began.
        if (current.size() >= 4 && near(first, last)) {
            current.pop_back();
            outlines.push_back(std::move(current));
        }
        current.clear();
    };
    for (std::size_t index = 0; index < drawing.shapeCount(); ++index) {
        auto* shape = drawing.shape(index);
        if (shape == nullptr || !(shape->length() > 0)) { continue; }
        const auto start = shape->nextVector(0);
        if (current.empty() || !near(start, last)) {
            finish();
            first = start;
            current.push_back({start.x, start.y});
        }
        const auto steps = dynamic_cast<osci::Line*>(shape) != nullptr ? 1 : 12;
        for (int step = 1; step <= steps; ++step) {
            const auto point = shape->nextVector(static_cast<float>(step) / steps);
            current.push_back({point.x, point.y});
        }
        last = shape->nextVector(1);
    }
    finish();
    if (outlines.empty()) { return mesh; }
    // One region at a time: an outline with its holes, from the union.
    Clipper2Lib::ClipperD clipper(5);
    clipper.AddSubject(outlines);
    Clipper2Lib::PolyTreeD tree;
    if (!clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, tree)) { return mesh; }
    const auto region = [&](auto&& self, const Clipper2Lib::PolyPathD& outer) -> void {
        std::vector<const Clipper2Lib::PathD*> rings {&outer.Polygon()};
        for (std::size_t index = 0; index < outer.Count(); ++index) { rings.push_back(&outer.Child(index)->Polygon()); }
        detail::slabs(rings, mesh);
        // Islands inside the holes.
        for (std::size_t index = 0; index < outer.Count(); ++index) {
            const auto* hole = outer.Child(index);
            for (std::size_t island = 0; island < hole->Count(); ++island) { self(self, *hole->Child(island)); }
        }
    };
    for (std::size_t index = 0; index < tree.Count(); ++index) { region(region, *tree.Child(index)); }
    return mesh;
}
}
