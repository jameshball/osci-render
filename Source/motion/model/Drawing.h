#pragma once

#include <JuceHeader.h>
#include <optional>
#include <vector>

// A shape drawn in osci-motion: strokes of Bezier anchors in output space
// (the unit square, y up). Drawings are saved as SVG marked as a drawing, so
// they import exactly where they were drawn and can be edited again.
namespace motion::drawing {
using Point = juce::Point<float>;

struct Anchor {
    Point point, in, out; // Handles are absolute; equal to `point` when sharp.
    bool smooth = false;
    static Anchor corner(Point at) { return {at, at, at, false}; }
    bool operator==(const Anchor&) const = default;
};

struct Stroke {
    std::vector<Anchor> anchors;
    bool closed = false;
    bool operator==(const Stroke&) const = default;
};

struct Drawing {
    std::vector<Stroke> strokes;
    bool empty() const { return std::none_of(strokes.begin(), strokes.end(), [](const auto& stroke) { return stroke.anchors.size() > 1; }); }
    bool operator==(const Drawing&) const = default;
};

inline constexpr const char* marker = "data-osci-motion-drawing";

// The stroke as a JUCE path (y up, as drawn).
inline juce::Path toPath(const Stroke& stroke) {
    juce::Path path;
    if (stroke.anchors.empty()) { return path; }
    path.startNewSubPath(stroke.anchors.front().point);
    const auto segment = [&path](const Anchor& from, const Anchor& to) {
        if (from.out == from.point && to.in == to.point) { path.lineTo(to.point); } else { path.cubicTo(from.out, to.in, to.point); }
    };
    for (std::size_t index = 1; index < stroke.anchors.size(); ++index) { segment(stroke.anchors[index - 1], stroke.anchors[index]); }
    if (stroke.closed && stroke.anchors.size() > 1) {
        segment(stroke.anchors.back(), stroke.anchors.front());
        path.closeSubPath();
    }
    return path;
}

inline juce::Path toPath(const Drawing& drawing) {
    juce::Path path;
    for (const auto& stroke : drawing.strokes) {
        if (stroke.anchors.size() > 1) { path.addPath(toPath(stroke)); }
    }
    return path;
}

// SVG path data with y flipped (SVG y runs down).
inline juce::String pathData(const Stroke& stroke) {
    const auto number = [](float value) { return juce::String(value, 5).trimCharactersAtEnd("0").trimCharactersAtEnd("."); };
    const auto at = [&number](Point point) { return number(point.x) + " " + number(-point.y); };
    juce::String data;
    if (stroke.anchors.size() < 2) { return data; }
    data << "M" << at(stroke.anchors.front().point);
    const auto segment = [&](const Anchor& from, const Anchor& to) {
        if (from.out == from.point && to.in == to.point) { data << " L" << at(to.point); } else { data << " C" << at(from.out) << " " << at(to.in) << " " << at(to.point); }
    };
    for (std::size_t index = 1; index < stroke.anchors.size(); ++index) { segment(stroke.anchors[index - 1], stroke.anchors[index]); }
    if (stroke.closed) {
        segment(stroke.anchors.back(), stroke.anchors.front());
        data << " Z";
    }
    return data;
}

// Which anchors are smooth, so editing keeps their handles mirrored.
inline juce::String smoothFlags(const Stroke& stroke) {
    juce::String flags;
    for (const auto& anchor : stroke.anchors) { flags << (anchor.smooth ? "1" : "0"); }
    return flags;
}

inline juce::String toSvg(const Drawing& drawing) {
    juce::String svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-1 -1 2 2\" " << marker << "=\"1\">\n";
    for (const auto& stroke : drawing.strokes) {
        const auto data = pathData(stroke);
        if (data.isEmpty()) { continue; }
        svg << "  <path d=\"" << data << "\" fill=\"none\" stroke=\"#72de98\" stroke-width=\"0.01\" data-smooth=\"" << smoothFlags(stroke) << "\"/>\n";
    }
    svg << "</svg>\n";
    return svg;
}

inline bool isDrawing(const juce::String& svg) { return svg.contains(marker); }

// Each path element of a marked drawing, as a JUCE path in drawn (y up) space.
inline std::vector<std::pair<juce::Path, juce::String>> paths(const juce::String& svg) {
    std::vector<std::pair<juce::Path, juce::String>> result;
    const auto xml = juce::parseXML(svg);
    if (xml == nullptr || !xml->hasAttribute(marker)) { return result; }
    for (const auto* element : xml->getChildWithTagNameIterator("path")) {
        auto path = juce::Drawable::parseSVGPath(element->getStringAttribute("d"));
        path.applyTransform(juce::AffineTransform::scale(1.0f, -1.0f));
        result.emplace_back(std::move(path), element->getStringAttribute("data-smooth"));
    }
    return result;
}

// The strokes back from a saved drawing, so it can be edited again.
inline std::optional<Drawing> fromSvg(const juce::String& svg) {
    if (!isDrawing(svg)) { return std::nullopt; }
    Drawing drawing;
    for (const auto& [path, smooth] : paths(svg)) {
        Stroke stroke;
        // Elements are read ahead so only a segment followed by Z closes.
        struct Element { juce::Path::Iterator::PathElementType type; float x1, y1, x2, y2, x3, y3; };
        std::vector<Element> elements;
        juce::Path::Iterator iterator(path);
        while (iterator.next()) { elements.push_back({iterator.elementType, iterator.x1, iterator.y1, iterator.x2, iterator.y2, iterator.x3, iterator.y3}); }
        for (std::size_t at = 0; at < elements.size(); ++at) {
            const auto& element = elements[at];
            const auto closesNext = at + 1 < elements.size() && elements[at + 1].type == juce::Path::Iterator::closePath;
            if (element.type == juce::Path::Iterator::startNewSubPath) {
                if (!stroke.anchors.empty()) { drawing.strokes.push_back(std::exchange(stroke, {})); }
                stroke.anchors.push_back(Anchor::corner({element.x1, element.y1}));
            } else if (element.type == juce::Path::Iterator::lineTo || element.type == juce::Path::Iterator::cubicTo) {
                const auto cubic = element.type == juce::Path::Iterator::cubicTo;
                const Point end = cubic ? Point(element.x3, element.y3) : Point(element.x1, element.y1);
                if (stroke.anchors.empty()) { continue; }
                if (cubic) { stroke.anchors.back().out = {element.x1, element.y1}; }
                // A closing segment ends on the first anchor.
                const auto& first = stroke.anchors.front();
                if (closesNext && stroke.anchors.size() > 1 && end.getDistanceFrom(first.point) < 1.0e-5f) {
                    stroke.anchors.front().in = cubic ? Point(element.x2, element.y2) : first.point;
                    stroke.closed = true;
                    continue;
                }
                auto anchor = Anchor::corner(end);
                if (cubic) { anchor.in = {element.x2, element.y2}; }
                stroke.anchors.push_back(anchor);
            } else if (element.type == juce::Path::Iterator::closePath) {
                stroke.closed = true;
            }
        }
        for (int index = 0; index < smooth.length() && index < static_cast<int>(stroke.anchors.size()); ++index) { stroke.anchors[static_cast<std::size_t>(index)].smooth = smooth[index] == '1'; }
        if (!stroke.anchors.empty()) { drawing.strokes.push_back(std::move(stroke)); }
    }
    return drawing;
}

// Freehand input to a few smooth anchors: drop points within `tolerance` of
// the line through their neighbours (Ramer-Douglas-Peucker), then give the
// survivors Catmull-Rom handles.
inline Stroke simplify(const std::vector<Point>& input, float tolerance, bool closed = false) {
    Stroke stroke;
    stroke.closed = closed;
    if (input.size() < 2) {
        for (const auto& point : input) { stroke.anchors.push_back(Anchor::corner(point)); }
        return stroke;
    }
    std::vector<bool> keep(input.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> spans {{0, input.size() - 1}};
    while (!spans.empty()) {
        const auto [first, last] = spans.back();
        spans.pop_back();
        float worst = 0;
        std::size_t index = first;
        const juce::Line<float> chord(input[first], input[last]);
        for (auto candidate = first + 1; candidate < last; ++candidate) {
            Point nearest;
            const auto distance = chord.getLength() > 0 ? chord.getDistanceFromPoint(input[candidate], nearest) : input[candidate].getDistanceFrom(input[first]);
            if (distance > worst) {
                worst = distance;
                index = candidate;
            }
        }
        if (worst > tolerance) {
            keep[index] = true;
            spans.emplace_back(first, index);
            spans.emplace_back(index, last);
        }
    }
    std::vector<Point> points;
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (keep[index]) { points.push_back(input[index]); }
    }
    const auto count = points.size();
    for (std::size_t index = 0; index < count; ++index) {
        const auto& point = points[index];
        const auto previous = index > 0 ? points[index - 1] : (closed ? points[count - 1] : point);
        const auto next = index + 1 < count ? points[index + 1] : (closed ? points[0] : point);
        const auto tangent = (next - previous) / 6.0f;
        stroke.anchors.push_back({point, point - tangent, point + tangent, count > 2});
    }
    return stroke;
}

inline Stroke rectangle(Point a, Point b) {
    Stroke stroke;
    stroke.closed = true;
    for (const auto point : {Point(a.x, a.y), Point(b.x, a.y), Point(b.x, b.y), Point(a.x, b.y)}) { stroke.anchors.push_back(Anchor::corner(point)); }
    return stroke;
}

// Four smooth anchors with the usual circle-approximation handles.
inline Stroke ellipse(Point centre, Point radius) {
    constexpr float kappa = 0.5522847f;
    Stroke stroke;
    stroke.closed = true;
    const Point offsets[] {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
    for (std::size_t index = 0; index < 4; ++index) {
        const auto direction = offsets[index];
        const Point tangent(direction.y * radius.x * kappa, -direction.x * radius.y * kappa);
        const Point point(centre.x + direction.x * radius.x, centre.y + direction.y * radius.y);
        stroke.anchors.push_back({point, point - tangent, point + tangent, true});
    }
    return stroke;
}
}
