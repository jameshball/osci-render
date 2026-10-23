#pragma once

#include "PreparedDrawing.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace motion::mesh {
// The faces a wireframe's lines enclose, for meshes known only by their lines
// (OBJ line elements, parts split from a mesh). Its triangles are faces, and so
// are its four-sided loops with neither diagonal an edge: a cube's six squares.
// A quad whose diagonal is a line is two of its triangles instead.
inline PreparedDrawing::Mesh facesOfLines(const std::vector<std::unique_ptr<osci::Shape>>& shapes) {
    std::vector<osci::Point> points;
    std::map<std::tuple<long long, long long, long long>, int> indices;
    const auto index = [&](const osci::Point& point) {
        const auto key = std::make_tuple(std::llround(point.x * 1.0e4), std::llround(point.y * 1.0e4), std::llround(point.z * 1.0e4));
        const auto found = indices.find(key);
        if (found != indices.end()) { return found->second; }
        points.push_back(point);
        return indices[key] = static_cast<int>(points.size()) - 1;
    };
    std::set<std::pair<int, int>> edges;
    for (const auto& shape : shapes) {
        if (shape == nullptr || dynamic_cast<const osci::Line*>(shape.get()) == nullptr || !(shape->length() > 0)) { continue; }
        const auto a = index(shape->nextVector(0)), b = index(shape->nextVector(1));
        if (a != b) { edges.insert({std::min(a, b), std::max(a, b)}); }
    }
    PreparedDrawing::Mesh faces;
    // Meshes are lines from vertex to vertex; anything else has no faces.
    if (edges.size() < 3 || edges.size() > 200000) { return faces; }
    faces.vertices = points;
    const auto triangle = [&](int a, int b, int c) { faces.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)}); };
    std::vector<std::vector<int>> neighbours(points.size());
    for (const auto& [a, b] : edges) {
        neighbours[static_cast<std::size_t>(a)].push_back(b);
        neighbours[static_cast<std::size_t>(b)].push_back(a);
    }
    for (auto& list : neighbours) { std::sort(list.begin(), list.end()); }
    const auto joined = [&](int a, int b) { return edges.contains({std::min(a, b), std::max(a, b)}); };
    // Triangles: each found once, from its smallest vertex.
    for (const auto& [a, b] : edges) {
        for (const auto c : neighbours[static_cast<std::size_t>(b)]) {
            if (c > b && joined(a, c)) { triangle(a, b, c); }
        }
    }
    // Quads a-b-c-d: a and c share neighbours b and d, with no diagonal.
    std::set<std::array<int, 4>> quads;
    for (std::size_t corner = 0; corner < neighbours.size(); ++corner) {
        const auto& around = neighbours[corner];
        // A dense vertex is not a quad mesh's; skip what would be costly.
        if (around.size() > 64) { continue; }
        for (std::size_t i = 0; i < around.size(); ++i) {
            for (std::size_t j = i + 1; j < around.size(); ++j) {
                const auto a = around[i], c = around[j];
                if (joined(a, c)) { continue; }
                for (const auto d : neighbours[static_cast<std::size_t>(a)]) {
                    const auto b = static_cast<int>(corner);
                    if (d == b || !joined(d, c) || joined(b, d)) { continue; }
                    std::array<int, 4> key {a, b, c, d};
                    std::sort(key.begin(), key.end());
                    if (!quads.insert(key).second) { continue; }
                    triangle(a, b, c);
                    triangle(a, c, d);
                }
            }
        }
    }
    return faces;
}
}
