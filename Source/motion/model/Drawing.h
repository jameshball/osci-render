#pragma once

#include <JuceHeader.h>
#include <array>
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

namespace detail {
inline Point normalised(Point value) {
    const auto length = value.getDistanceFromOrigin();
    return length > 1.0e-9f ? value / length : Point();
}
inline Point bezier(const std::array<Point, 4>& c, float t) {
    const auto u = 1.0f - t;
    return c[0] * (u * u * u) + c[1] * (3 * u * u * t) + c[2] * (3 * u * t * t) + c[3] * (t * t * t);
}
// Least-squares control points for fixed end tangents (Schneider 1990).
inline std::array<Point, 4> fitCubic(const std::vector<Point>& points, std::size_t first, std::size_t last, const std::vector<float>& u, Point left, Point right) {
    const auto start = points[first], end = points[last];
    float c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
    for (std::size_t i = first; i <= last; ++i) {
        const auto t = u[i - first], v = 1.0f - t;
        const auto b0 = v * v * v, b1 = 3 * v * v * t, b2 = 3 * v * t * t, b3 = t * t * t;
        const auto a1 = left * b1, a2 = right * b2;
        c00 += a1.getDotProduct(a1);
        c01 += a1.getDotProduct(a2);
        c11 += a2.getDotProduct(a2);
        const auto rest = points[i] - (start * (b0 + b1) + end * (b2 + b3));
        x0 += a1.getDotProduct(rest);
        x1 += a2.getDotProduct(rest);
    }
    const auto determinant = c00 * c11 - c01 * c01;
    const auto chord = start.getDistanceFrom(end);
    auto alphaLeft = std::abs(determinant) > 1.0e-12f ? (x0 * c11 - x1 * c01) / determinant : 0.0f;
    auto alphaRight = std::abs(determinant) > 1.0e-12f ? (c00 * x1 - c01 * x0) / determinant : 0.0f;
    // Degenerate solutions fall back to a third of the chord (Wu-Barsky).
    if (alphaLeft < chord * 1.0e-3f || alphaRight < chord * 1.0e-3f || alphaLeft > chord * 3 || alphaRight > chord * 3) { alphaLeft = alphaRight = chord / 3; }
    return {start, start + left * alphaLeft, end + right * alphaRight, end};
}
// One Newton step towards each point's nearest parameter on the curve.
inline void reparameterise(const std::vector<Point>& points, std::size_t first, std::vector<float>& u, const std::array<Point, 4>& c) {
    for (std::size_t i = 0; i < u.size(); ++i) {
        const auto t = u[i], v = 1.0f - t;
        const auto q = bezier(c, t) - points[first + i];
        const auto d1 = (c[1] - c[0]) * (3 * v * v) + (c[2] - c[1]) * (6 * v * t) + (c[3] - c[2]) * (3 * t * t);
        const auto d2 = (c[2] - c[1] * 2.0f + c[0]) * (6 * v) + (c[3] - c[2] * 2.0f + c[1]) * (6 * t);
        const auto denominator = d1.getDotProduct(d1) + q.getDotProduct(d2);
        if (std::abs(denominator) > 1.0e-12f) { u[i] = std::clamp(t - q.getDotProduct(d1) / denominator, 0.0f, 1.0f); }
    }
}
inline void fitRange(const std::vector<Point>& points, std::size_t first, std::size_t last, Point left, Point right, float tolerance, std::vector<std::array<Point, 4>>& out, int depth = 0) {
    if (last - first == 1 || depth > 24) {
        const auto chord = points[first].getDistanceFrom(points[last]) / 3;
        out.push_back({points[first], points[first] + left * chord, points[last] + right * chord, points[last]});
        return;
    }
    std::vector<float> u {0.0f};
    for (auto i = first + 1; i <= last; ++i) { u.push_back(u.back() + points[i].getDistanceFrom(points[i - 1])); }
    for (auto& value : u) { value = u.back() > 0 ? value / u.back() : 0.0f; }
    auto curve = fitCubic(points, first, last, u, left, right);
    std::size_t split = (first + last) / 2;
    for (int iteration = 0; iteration < 5; ++iteration) {
        float worst = 0;
        for (auto i = first + 1; i < last; ++i) {
            const auto error = bezier(curve, u[i - first]).getDistanceFrom(points[i]);
            if (error > worst) {
                worst = error;
                split = i;
            }
        }
        if (worst <= tolerance) {
            out.push_back(curve);
            return;
        }
        if (worst > tolerance * 4) { break; }
        reparameterise(points, first, u, curve);
        curve = fitCubic(points, first, last, u, left, right);
    }
    // Split at the worst point with a shared tangent, so the join is smooth.
    const auto centre = normalised(points[split - 1] - points[split + 1]);
    fitRange(points, first, split, left, centre, tolerance, out, depth + 1);
    fitRange(points, split, last, -centre, right, tolerance, out, depth + 1);
}
// Indices of sharp turns: the direction over a few points either side
// changes by more than about 55 degrees, keeping the sharpest of a run.
inline std::vector<std::size_t> corners(const std::vector<Point>& points, float reach) {
    std::vector<std::size_t> result;
    std::vector<float> turn(points.size(), 0.0f);
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        std::size_t back = i, ahead = i;
        while (back > 0 && points[back].getDistanceFrom(points[i]) < reach) { --back; }
        while (ahead + 1 < points.size() && points[ahead].getDistanceFrom(points[i]) < reach) { ++ahead; }
        const auto in = normalised(points[i] - points[back]), out = normalised(points[ahead] - points[i]);
        turn[i] = std::acos(std::clamp(in.getDotProduct(out), -1.0f, 1.0f));
    }
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        if (turn[i] < .95f) { continue; }
        std::size_t j = i;
        while (j + 1 < points.size() && turn[j + 1] >= .95f) { ++j; }
        const auto sharpest = static_cast<std::size_t>(std::max_element(turn.begin() + static_cast<std::ptrdiff_t>(i), turn.begin() + static_cast<std::ptrdiff_t>(j) + 1) - turn.begin());
        result.push_back(sharpest);
        i = j;
    }
    return result;
}
}

// Freehand input to a few Bezier segments: light smoothing removes hand
// jitter, sharp turns become corners, and each run between corners is fitted
// with as few cubic curves as stay within `tolerance` (Schneider's method).
inline Stroke fit(std::vector<Point> input, float tolerance, bool closed = false) {
    Stroke stroke;
    stroke.closed = closed;
    input.erase(std::unique(input.begin(), input.end()), input.end());
    if (input.size() < 3) {
        for (const auto& point : input) { stroke.anchors.push_back(Anchor::corner(point)); }
        return stroke;
    }
    const auto turns = detail::corners(input, tolerance * 6);
    // Three passes of neighbour averaging, holding the ends and corners.
    for (int pass = 0; pass < 3; ++pass) {
        auto smoothed = input;
        for (std::size_t i = 1; i + 1 < input.size(); ++i) {
            if (std::find(turns.begin(), turns.end(), i) != turns.end()) { continue; }
            smoothed[i] = input[i - 1] * .25f + input[i] * .5f + input[i + 1] * .25f;
        }
        input = std::move(smoothed);
    }
    std::vector<std::size_t> breaks {0};
    breaks.insert(breaks.end(), turns.begin(), turns.end());
    breaks.push_back(input.size() - 1);
    std::vector<std::array<Point, 4>> curves;
    std::vector<bool> sharp;
    for (std::size_t piece = 0; piece + 1 < breaks.size(); ++piece) {
        const auto first = breaks[piece], last = breaks[piece + 1];
        if (last <= first) { continue; }
        const auto look = std::min<std::size_t>(3, last - first);
        auto left = detail::normalised(input[first + look] - input[first]);
        auto right = detail::normalised(input[last - look] - input[last]);
        // A closed loop without corners joins smoothly where it started.
        if (closed && turns.empty() && input.size() > 2 * look + 1) {
            const auto through = detail::normalised(input[first + look] - input[last - look]);
            left = through;
            right = -through;
        }
        const auto before = curves.size();
        detail::fitRange(input, first, last, left, right, tolerance, curves);
        sharp.resize(curves.size(), false);
        if (before > 0) { sharp[before] = true; }
    }
    if (curves.empty()) { return stroke; }
    stroke.anchors.push_back({curves.front()[0], curves.front()[0], curves.front()[1], false});
    for (std::size_t index = 0; index < curves.size(); ++index) {
        stroke.anchors.back().out = curves[index][1];
        const auto& curve = curves[index];
        const auto smoothJoin = index + 1 < curves.size() && !sharp[index + 1];
        stroke.anchors.push_back({curve[3], curve[2], curve[3], smoothJoin});
    }
    if (closed && stroke.anchors.size() > 2 && stroke.anchors.back().point.getDistanceFrom(stroke.anchors.front().point) < tolerance * 4) {
        stroke.anchors.front().in = stroke.anchors.back().in;
        stroke.anchors.front().smooth = turns.empty();
        stroke.anchors.pop_back();
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
