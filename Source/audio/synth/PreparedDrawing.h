#pragma once

#include <osci_render_core/osci_render_core.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace osci {

// Prepared off the render thread. Lengths and search boundaries never change
// during playback; product timelines and MIDI voices may share the geometry.
class PreparedDrawing {
public:
    explicit PreparedDrawing(std::vector<std::unique_ptr<Shape>> source) : shapes(std::move(source)) {
        ends.reserve(shapes.size());
        Point firstStart, previousEnd;
        bool havePositive = false;
        for (std::size_t index = 0; index < shapes.size(); ++index) {
            auto& shape = shapes[index];
            const auto length = shape != nullptr ? static_cast<double>(shape->length()) : 0.0;
            const auto start = total;
            total += std::isfinite(length) ? std::max(0.0, length) : 0.0;
            ends.push_back(total);
            if (total > start) {
                const auto beginning = shape->nextVector(0);
                if (havePositive) {
                    if (!connected(previousEnd, beginning)) { discontinuities.push_back(start); }
                } else {
                    firstStart = beginning;
                    havePositive = true;
                }
                previousEnd = shape->nextVector(1);
                lastPositive = index;
            }
        }
        if (havePositive) {
            for (auto& boundary : discontinuities) { boundary /= total; }
            if (!connected(previousEnd, firstStart)) { discontinuities.insert(discontinuities.begin(), 0.0); }
        }
        prepareTraversal();
    }

    // phaseSpan is the phase distance travelled per output sample. Symmetric
    // blanking prevents sampling either side of a skipped spatial jump while
    // preserving the sampled XYZ. Zero keeps the original colour semantics.
    Point sample(double phase, double phaseSpan = 0) const {
        if (shapes.empty() || total <= 0.0 || !std::isfinite(phase)) {
            return Point(0, 0, 0, 0, 0, 0);
        }
        const auto clampedPhase = std::clamp(phase, 0.0, std::nextafter(1.0, 0.0));
        const auto distance = clampedPhase * total;
        const auto boundary = std::upper_bound(ends.begin(), ends.end(), distance);
        const auto index = boundary == ends.end() ? lastPositive : static_cast<std::size_t>(boundary - ends.begin());
        const auto start = index == 0 ? 0.0 : ends[index - 1];
        const auto length = ends[index] - start;
        auto point = shapes[index]->nextVector(static_cast<float>(length > 0 ? (distance - start) / length : 0.0));
        if (!std::isfinite(phaseSpan) || phaseSpan < 0 || (phaseSpan > 0 && crossesDiscontinuity(clampedPhase, phaseSpan))) {
            point.r = point.g = point.b = 0;
        }
        return point;
    }

    // Every positive segment contributes its endpoint; each stroke adds a lit
    // start and two dark guards. Zero means there is no usable traversal.
    std::int64_t minimumTraversalSamples() const { return traversalMinimum; }

    // Independent from sample(): an integer budget covers complete strokes,
    // including corners and closed-path seams. Extra samples are apportioned by
    // cumulative segment length, then use the shape's native parameterization.
    // This is not arc-length reparameterization within a curved shape.
    Point sampleTraversal(std::int64_t index, std::int64_t count) const {
        if (traversalMinimum == 0 || count < traversalMinimum || index < 0 || index >= count) {
            return Point(0, 0, 0, 0, 0, 0);
        }
        const auto extra = count - traversalMinimum;
        const auto stroke = std::lower_bound(strokes.begin(), strokes.end(), index, [&](const TraversalStroke& item, std::int64_t value) {
            const auto& last = segments[item.last];
            return last.baseEnd + 1 + extraThrough(last, extra) < value;
        });
        if (stroke == strokes.end()) { return Point(0, 0, 0, 0, 0, 0); }
        const auto& first = segments[stroke->first];
        const auto& last = segments[stroke->last];
        const auto beforeExtra = stroke->first == 0 ? 0 : extraThrough(segments[stroke->first - 1], extra);
        const auto start = first.baseEnd - 2 + beforeExtra;
        const auto end = last.baseEnd + 1 + extraThrough(last, extra);
        if (index <= start + 1) {
            auto point = shapes[first.shape]->nextVector(0);
            if (index == start) { point.r = point.g = point.b = 0; }
            return point;
        }
        if (index == end) {
            auto point = shapes[last.shape]->nextVector(1);
            point.r = point.g = point.b = 0;
            return point;
        }
        const auto segment = std::lower_bound(segments.begin() + stroke->first, segments.begin() + stroke->last + 1, index,
            [&](const TraversalSegment& item, std::int64_t value) { return item.baseEnd + extraThrough(item, extra) < value; });
        const auto segmentIndex = static_cast<std::size_t>(segment - segments.begin());
        const auto previousEnd = segmentIndex == stroke->first ? start + 1
            : segments[segmentIndex - 1].baseEnd + extraThrough(segments[segmentIndex - 1], extra);
        const auto segmentEnd = segment->baseEnd + extraThrough(*segment, extra);
        const auto progress = static_cast<float>(static_cast<double>(index - previousEnd) / static_cast<double>(segmentEnd - previousEnd));
        return shapes[segment->shape]->nextVector(progress);
    }

    bool empty() const { return shapes.empty() || total <= 0.0; }
    double length() const { return total; }

private:
    struct TraversalSegment {
        std::size_t shape;
        std::int64_t baseEnd;
        double lengthEnd;
    };
    struct TraversalStroke { std::size_t first, last; };

    std::int64_t extraThrough(const TraversalSegment& segment, std::int64_t extra) const {
        if (segment.lengthEnd == total) { return extra; }
        const auto apportioned = (static_cast<long double>(segment.lengthEnd) * static_cast<long double>(extra)) / static_cast<long double>(total);
        // Compare before conversion, including on platforms where long double
        // has only double precision and INT64_MAX rounds up to 2^63.
        if (apportioned >= static_cast<long double>(extra)) { return extra; }
        if (apportioned <= 0) { return 0; }
        return static_cast<std::int64_t>(apportioned);
    }
    void prepareTraversal() {
        if (!(total > 0) || !std::isfinite(total)
            || shapes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() / 4)) { return; }
        Point previous;
        bool havePrevious = false;
        std::int64_t reserved = 0;
        for (std::size_t index = 0; index < shapes.size(); ++index) {
            const auto before = index == 0 ? 0.0 : ends[index - 1];
            if (!(ends[index] > before)) { continue; }
            const auto start = shapes[index]->nextVector(0);
            const auto end = shapes[index]->nextVector(1);
            if (!connected(start, start) || !connected(end, end)) {
                segments.clear();
                strokes.clear();
                return;
            }
            if (!havePrevious || !connected(previous, start)) {
                if (havePrevious) { ++reserved; } // Previous stroke's dark end.
                strokes.push_back({segments.size(), segments.size()});
                reserved += 2; // Dark start followed by lit start.
            }
            segments.push_back({index, reserved++, ends[index]});
            strokes.back().last = segments.size() - 1;
            previous = end;
            havePrevious = true;
        }
        if (havePrevious) { traversalMinimum = reserved + 1; }
    }

    static bool connected(const Point& a, const Point& b) {
        // Absolute tolerance accommodates float evaluation at a shared curve
        // endpoint. A relative tolerance would hide large jumps for far-away
        // geometry, so it deliberately does not scale with world coordinates.
        constexpr double tolerance = 1.0e-6;
        const auto same = [](float a, float b) {
            return std::isfinite(a) && std::isfinite(b) && std::abs(static_cast<double>(a) - b) <= tolerance;
        };
        return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
    }
    bool crossesDiscontinuity(double phase, double span) const {
        if (discontinuities.empty()) { return false; }
        if (span >= 0.5) { return true; }
        const auto intersects = [&](double low, double high) {
            const auto found = std::lower_bound(discontinuities.begin(), discontinuities.end(), low);
            return found != discontinuities.end() && *found <= high;
        };
        const auto low = phase - span, high = phase + span;
        if (intersects(std::max(0.0, low), std::min(1.0, high))) { return true; }
        if (low < 0 && intersects(1.0 + low, 1.0)) { return true; }
        return high >= 1.0 && intersects(0.0, high - 1.0);
    }
    std::vector<TraversalSegment> segments;
    std::vector<TraversalStroke> strokes;
    std::int64_t traversalMinimum = 0;
    std::vector<std::unique_ptr<Shape>> shapes;
    std::vector<double> ends;
    std::vector<double> discontinuities;
    std::size_t lastPositive = 0;
    double total = 0.0;
};

}
