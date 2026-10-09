#pragma once

#include "Document.h"
#include "Drawing.h"
#include "PreparedSource.h"
#include <cmath>
#include <numbers>
#include <optional>
#include <set>

// Parts of a still vector source (text, SVG, OBJ, a still GPLA or a drawing):
// its exact shapes, picked in the Scene and split off as a source of their
// own. Flat parts become an editable drawing; 3D parts become a line set.
namespace motion::parts {

// The drawing whose shapes can be picked, or null: a still vector source.
inline const PreparedDrawing* drawingOf(const Asset& asset) {
    if (asset.liveIdentity != nullptr || asset.source == nullptr || asset.source->frameCount() != 1) { return nullptr; }
    return asset.source->drawingAt(0);
}

// Why a source's parts cannot be picked; empty when they can.
inline juce::String unavailableReason(const Asset& asset) {
    if (drawingOf(asset) != nullptr) { return {}; }
    if (asset.liveIdentity != nullptr) { return "Live sources are picked whole"; }
    if (asset.source != nullptr && asset.source->frameCount() > 1) { return "Animated sources are picked whole"; }
    return "Lua, image, video and fractal sources are picked whole";
}

namespace detail {
inline bool same(const osci::Point& a, const osci::Point& b) {
    constexpr float tolerance = 1.0e-5f;
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
}

// Each shape's path: shapes joined end to start share one, so a letter's
// outline or a polyline is one path.
inline std::vector<std::size_t> pathIndices(const PreparedDrawing& drawing) {
    std::vector<std::size_t> paths(drawing.shapeCount(), 0);
    std::size_t path = 0;
    std::optional<osci::Point> previousEnd;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        auto* shape = drawing.shape(index);
        if (shape == nullptr) {
            paths[index] = path;
            continue;
        }
        const auto start = shape->nextVector(0);
        if (previousEnd.has_value() && !detail::same(*previousEnd, start)) { ++path; }
        paths[index] = path;
        previousEnd = shape->nextVector(1);
    }
    return paths;
}

// Flat when every shape lies in z = 0 (curves are always flat).
inline bool flat(const PreparedDrawing& drawing, const std::set<std::size_t>& shapes) {
    for (const auto index : shapes) {
        const auto* line = dynamic_cast<const osci::Line*>(drawing.shape(index));
        if (line != nullptr && (line->z1 != 0 || line->z2 != 0)) { return false; }
    }
    return true;
}

// The shapes as drawing strokes: joined shapes continue a stroke, and a
// stroke that ends where it began is closed. Arcs become cubic segments.
inline drawing::Drawing toDrawing(const PreparedDrawing& source, const std::set<std::size_t>& shapes) {
    using drawing::Anchor;
    drawing::Drawing result;
    const auto point = [](float x, float y) { return drawing::Point(x, y); };
    std::optional<osci::Point> previousEnd;
    const auto cubic = [&](drawing::Stroke& stroke, drawing::Point out, drawing::Point in, drawing::Point to) {
        stroke.anchors.back().out = out;
        stroke.anchors.push_back({to, in, to, false});
    };
    for (const auto index : shapes) {
        auto* shape = source.shape(index);
        if (shape == nullptr || !(shape->length() > 0)) { continue; }
        const auto start = shape->nextVector(0);
        if (result.strokes.empty() || !previousEnd.has_value() || !detail::same(*previousEnd, start)) {
            result.strokes.push_back({});
            result.strokes.back().anchors.push_back(Anchor::corner(point(start.x, start.y)));
        }
        auto& stroke = result.strokes.back();
        const auto* curve = dynamic_cast<const osci::CubicBezierCurve*>(shape);
        const auto* arc = dynamic_cast<const osci::CircleArc*>(shape);
        if (curve != nullptr) {
            cubic(stroke, point(curve->x2, curve->y2), point(curve->x3, curve->y3), point(curve->x4, curve->y4));
        } else if (arc != nullptr) {
            // Quarter turns at most, each as one cubic.
            const auto pieces = std::max(1, static_cast<int>(std::ceil(std::abs(arc->endAngle) / (std::numbers::pi / 2) - 1.0e-6)));
            const auto sweep = arc->endAngle / pieces;
            const auto kappa = 4.0f / 3.0f * std::tan(sweep / 4);
            for (int piece = 0; piece < pieces; ++piece) {
                const auto from = arc->startAngle + sweep * piece, to = from + sweep;
                const auto at = [&](float angle) { return point(arc->x + arc->radiusX * std::cos(angle), arc->y + arc->radiusY * std::sin(angle)); };
                const auto tangent = [&](float angle) { return point(-arc->radiusX * std::sin(angle), arc->radiusY * std::cos(angle)) * kappa; };
                cubic(stroke, at(from) + tangent(from), at(to) - tangent(to), at(to));
            }
        } else {
            const auto end = shape->nextVector(1);
            stroke.anchors.push_back(Anchor::corner(point(end.x, end.y)));
        }
        previousEnd = shape->nextVector(1);
    }
    for (auto& stroke : result.strokes) {
        auto& anchors = stroke.anchors;
        if (anchors.size() > 2 && anchors.back().point.getDistanceFrom(anchors.front().point) < 1.0e-5f) {
            anchors.front().in = anchors.back().in;
            anchors.pop_back();
            stroke.closed = true;
        }
        // Handles in line through their anchor keep moving together.
        for (auto& anchor : anchors) {
            const auto in = anchor.point - anchor.in, out = anchor.out - anchor.point;
            const auto cross = in.x * out.y - in.y * out.x;
            const auto lengths = in.getDistanceFromOrigin() * out.getDistanceFromOrigin();
            anchor.smooth = lengths > 1.0e-10f && std::abs(cross) <= 1.0e-3f * lengths && in.getDotProduct(out) > 0;
        }
    }
    std::erase_if(result.strokes, [](const auto& stroke) { return stroke.anchors.size() < 2; });
    return result;
}

// Line sets: OBJ line elements in the source's own space. The marker keeps
// them where they were cut, where other OBJ files are fitted to unit size.
inline constexpr const char* lineSetMarker = "# osci-motion line set";

inline bool isLineSet(const juce::String& content) { return content.startsWith(lineSetMarker); }

// Curves are divided into short lines.
inline juce::String toLineSet(const PreparedDrawing& source, const std::set<std::size_t>& shapes) {
    juce::String vertices, lines;
    int count = 0;
    const auto vertex = [&](const osci::Point& point) {
        vertices << "v " << juce::String(point.x, 6) << " " << juce::String(point.y, 6) << " " << juce::String(point.z, 6) << "\n";
        return ++count;
    };
    std::optional<osci::Point> previousEnd;
    int previousIndex = 0;
    for (const auto index : shapes) {
        auto* shape = source.shape(index);
        if (shape == nullptr || !(shape->length() > 0)) { continue; }
        const auto start = shape->nextVector(0);
        auto from = previousEnd.has_value() && detail::same(*previousEnd, start) ? previousIndex : vertex(start);
        const auto steps = dynamic_cast<const osci::Line*>(shape) != nullptr ? 1 : 16;
        for (int step = 1; step <= steps; ++step) {
            const auto to = vertex(shape->nextVector(static_cast<float>(step) / steps));
            lines << "l " << from << " " << to << "\n";
            from = to;
        }
        previousEnd = shape->nextVector(1);
        previousIndex = from;
    }
    return juce::String(lineSetMarker) + "\n" + vertices + lines;
}

// A line set's lines, exactly where they were cut.
inline std::vector<std::unique_ptr<osci::Shape>> lineSetShapes(const juce::String& content) {
    std::vector<osci::Point> vertices;
    std::vector<std::unique_ptr<osci::Shape>> shapes;
    juce::StringArray rows;
    rows.addLines(content);
    for (const auto& row : rows) {
        const auto tokens = juce::StringArray::fromTokens(row, " \t", "");
        if (tokens.size() == 4 && tokens[0] == "v") {
            vertices.emplace_back(tokens[1].getFloatValue(), tokens[2].getFloatValue(), tokens[3].getFloatValue());
        } else if (tokens.size() >= 3 && tokens[0] == "l") {
            for (int index = 2; index < tokens.size(); ++index) {
                const auto from = tokens[index - 1].getIntValue(), to = tokens[index].getIntValue();
                if (from < 1 || to < 1 || from > static_cast<int>(vertices.size()) || to > static_cast<int>(vertices.size())) { continue; }
                shapes.push_back(std::make_unique<osci::Line>(vertices[static_cast<std::size_t>(from - 1)], vertices[static_cast<std::size_t>(to - 1)]));
            }
        }
    }
    return shapes;
}

struct Split {
    juce::String part, rest; // file contents
    juce::String extension;  // ".svg" (a drawing) or ".obj" (a line set)
};

// The chosen shapes and the others, as two sources' file contents. Fails
// (nullopt) when either side would be empty.
inline std::optional<Split> split(const PreparedDrawing& source, const std::set<std::size_t>& chosen) {
    std::set<std::size_t> part, rest;
    for (std::size_t index = 0; index < source.shapeCount(); ++index) {
        auto* shape = source.shape(index);
        if (shape == nullptr || !(shape->length() > 0)) { continue; }
        (chosen.contains(index) ? part : rest).insert(index);
    }
    if (part.empty() || rest.empty()) { return std::nullopt; }
    std::set<std::size_t> all = part;
    all.insert(rest.begin(), rest.end());
    if (flat(source, all)) { return Split {drawing::toSvg(toDrawing(source, part)), drawing::toSvg(toDrawing(source, rest)), ".svg"}; }
    return Split {toLineSet(source, part), toLineSet(source, rest), ".obj"};
}
}
