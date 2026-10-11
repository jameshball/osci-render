#pragma once

#include <osci_render_core/osci_render_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace motion {

// Prepared off the render thread. Lengths and search boundaries never change
// during playback; product timelines may share the geometry.
class PreparedDrawing {
public:
    // A mesh's faces in the drawing's own space, as triangles of shared
    // vertices; lines behind faces can be hidden.
    struct Mesh {
        std::vector<osci::Point> vertices;
        std::vector<std::array<std::uint32_t, 3>> triangles;
        // A flat drawing's fill: it hides what is behind it, never its own lines.
        bool planar = false;
        bool empty() const { return triangles.empty(); }
    };

    // `fadeOpenEnds`: open strokes fade in and out rather than dwell lit at
    // their ends, for drawings whose strokes end where something hides them.
    // `weights`, one per shape when given: how much of the beam's time each
    // takes for its length, so a dimly lit part is drawn quickly and a bright
    // one slowly (1 is as any line).
    explicit PreparedDrawing(std::vector<std::unique_ptr<osci::Shape>> source, bool fadeOpenEnds = false, const std::vector<double>& weights = {}) : fading(fadeOpenEnds), shapes(std::move(source)) {
        ends.reserve(shapes.size());
        osci::Point firstStart, previousEnd;
        bool havePositive = false;
        for (std::size_t index = 0; index < shapes.size(); ++index) {
            auto& shape = shapes[index];
            const auto weight = index < weights.size() && std::isfinite(weights[index]) ? std::max(0.0, weights[index]) : 1.0;
            const auto geometric = shape != nullptr ? static_cast<double>(shape->length()) : 0.0;
            if (std::isfinite(geometric)) { drawn += std::max(0.0, geometric); }
            const auto length = geometric * weight;
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
    osci::Point sample(double phase, double phaseSpan = 0) const {
        if (shapes.empty() || total <= 0.0 || !std::isfinite(phase)) {
            return osci::Point(0, 0, 0, 0, 0, 0);
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

    // The fewest samples that draw every stroke completely: each stroke a dark
    // start, a lit start and a dark end, and a sample at each of its corners
    // (where it turns sharply), smooth runs between them walked by length.
    // Zero means there is no usable traversal.
    std::int64_t minimumTraversalSamples() const { return detailed.minimum; }
    // Fewer still, when even those do not fit: each stroke's ends alone, its
    // corners cut by however few samples it gets.
    std::int64_t coarseTraversalSamples() const { return coarse.minimum; }

    // Independent from sample(): an integer budget covers complete strokes,
    // including corners and closed-path seams (with too few for corners, their
    // ends alone). Extra samples are apportioned by cumulative length, then
    // use each shape's native parameterization. This is not arc-length
    // reparameterization within a curved shape.
    osci::Point sampleTraversal(std::int64_t index, std::int64_t count) const {
        const auto& traversal = count >= detailed.minimum ? detailed : coarse;
        if (traversal.minimum == 0 || count < traversal.minimum || index < 0 || index >= count) {
            return osci::Point(0, 0, 0, 0, 0, 0);
        }
        const auto& segments = traversal.segments;
        const auto& strokes = traversal.strokes;
        const auto extra = count - traversal.minimum;
        const auto stroke = std::lower_bound(strokes.begin(), strokes.end(), index, [&](const TraversalStroke& item, std::int64_t value) {
            const auto& last = segments[item.last];
            return last.baseEnd + 1 + extraThrough(last, extra) < value;
        });
        if (stroke == strokes.end()) { return osci::Point(0, 0, 0, 0, 0, 0); }
        const auto& first = segments[stroke->first];
        const auto& last = segments[stroke->last];
        const auto beforeExtra = stroke->first == 0 ? 0 : extraThrough(segments[stroke->first - 1], extra);
        const auto start = first.baseEnd - 2 + beforeExtra;
        const auto end = last.baseEnd + 1 + extraThrough(last, extra);
        // Where a segment's samples start and end.
        const auto bounds = [&](std::size_t segmentIndex) {
            const auto previousEnd = segmentIndex == stroke->first ? start + 1 : segments[segmentIndex - 1].baseEnd + extraThrough(segments[segmentIndex - 1], extra);
            return std::pair {previousEnd, segments[segmentIndex].baseEnd + extraThrough(segments[segmentIndex], extra)};
        };
        // A fading stroke is dark where it starts and ends, and comes up to
        // its light over its first step and goes down over its last. Every
        // sample is as bright as any other, so a sample bunched in near an
        // end would draw a bead there; a stroke whose end segment has a
        // single sample keeps it lit, so it is not lost.
        const auto ramps = [&](std::size_t segmentIndex) {
            const auto [previousEnd, segmentEnd] = bounds(segmentIndex);
            return stroke->fades && segmentEnd - previousEnd >= 2;
        };
        if (index <= start + 1) {
            auto point = shapes[first.firstShape]->nextVector(0);
            if (index == start || ramps(stroke->first)) { point.r = point.g = point.b = 0; }
            return point;
        }
        if (index == end || (index == end - 1 && ramps(stroke->last))) {
            auto point = shapes[last.lastShape]->nextVector(1);
            point.r = point.g = point.b = 0;
            return point;
        }
        const auto segment = std::lower_bound(segments.begin() + stroke->first, segments.begin() + stroke->last + 1, index,
            [&](const TraversalSegment& item, std::int64_t value) { return item.baseEnd + extraThrough(item, extra) < value; });
        const auto [previousEnd, segmentEnd] = bounds(static_cast<std::size_t>(segment - segments.begin()));
        const auto progress = static_cast<double>(index - previousEnd) / static_cast<double>(segmentEnd - previousEnd);
        return pointAlong(*segment, segment->lengthStart + (segment->lengthEnd - segment->lengthStart) * progress);
    }

    bool empty() const { return shapes.empty() || total <= 0.0; }
    // How long its lines are; and how much of the beam's time they take,
    // in the same units, once each is weighted (see the constructor).
    double length() const { return drawn; }
    double beamLength() const { return total; }
    // The exact geometry, for drawing and picking. Evaluating a shape leaves
    // it unchanged (lengths are cached at construction), as sample() relies on.
    std::size_t shapeCount() const { return shapes.size(); }
    osci::Shape* shape(std::size_t index) const { return index < shapes.size() ? shapes[index].get() : nullptr; }
    // A mesh's faces, set while it is prepared; empty for line drawings.
    const Mesh& faces() const { return meshFaces; }
    void setFaces(Mesh value) { meshFaces = std::move(value); }

private:
    Mesh meshFaces;
    bool fading = false;
    // A run of a stroke walked as one: shapes joined smoothly, from where the
    // stroke starts or turns a corner to the next.
    struct TraversalSegment {
        std::size_t firstShape, lastShape;
        std::int64_t baseEnd;
        double lengthStart, lengthEnd;
    };
    // A stroke dwells lit where it starts and ends, so a closed one's seam
    // shows like any corner. In a fading drawing an open one fades in and out
    // over a short way at its ends instead, so they draw no dot.
    struct TraversalStroke { std::size_t first, last; bool fades; };
    struct Traversal {
        std::vector<TraversalSegment> segments;
        std::vector<TraversalStroke> strokes;
        std::int64_t minimum = 0;
    };

    std::int64_t extraThrough(const TraversalSegment& segment, std::int64_t extra) const {
        if (segment.lengthEnd == total) { return extra; }
        const auto apportioned = (static_cast<long double>(segment.lengthEnd) * static_cast<long double>(extra)) / static_cast<long double>(total);
        // Compare before conversion, including on platforms where long double
        // has only double precision and INT64_MAX rounds up to 2^63.
        if (apportioned >= static_cast<long double>(extra)) { return extra; }
        if (apportioned <= 0) { return 0; }
        return static_cast<std::int64_t>(apportioned);
    }
    // The point `distance` along the drawing, within a run's shapes.
    osci::Point pointAlong(const TraversalSegment& segment, double distance) const {
        const auto found = std::lower_bound(ends.begin() + static_cast<std::ptrdiff_t>(segment.firstShape), ends.begin() + static_cast<std::ptrdiff_t>(segment.lastShape) + 1, distance);
        auto index = found == ends.begin() + static_cast<std::ptrdiff_t>(segment.lastShape) + 1 ? segment.lastShape : static_cast<std::size_t>(found - ends.begin());
        const auto startOf = [&](std::size_t shape) { return shape == 0 ? 0.0 : ends[shape - 1]; };
        while (index < segment.lastShape && !(ends[index] > startOf(index))) { ++index; }
        const auto from = startOf(index), length = ends[index] - from;
        const auto progress = length > 0 ? std::clamp((distance - from) / length, 0.0, 1.0) : 0.0;
        return shapes[index]->nextVector(static_cast<float>(progress));
    }
    // Whether a stroke turns sharply from one shape to the next, so the beam
    // must be there at a sample of its own to draw the corner.
    bool corner(std::size_t from, std::size_t to) const {
        constexpr float step = 1.0e-3f;
        const auto direction = [](osci::Point a, osci::Point b) {
            const auto x = static_cast<double>(b.x) - a.x, y = static_cast<double>(b.y) - a.y, z = static_cast<double>(b.z) - a.z;
            const auto length = std::sqrt(x * x + y * y + z * z);
            return length > 1.0e-12 ? std::array<double, 3> {x / length, y / length, z / length} : std::array<double, 3> {};
        };
        const auto leaving = direction(shapes[from]->nextVector(1 - step), shapes[from]->nextVector(1));
        const auto arriving = direction(shapes[to]->nextVector(0), shapes[to]->nextVector(step));
        // Turning by more than about 25 degrees.
        return leaving[0] * arriving[0] + leaving[1] * arriving[1] + leaving[2] * arriving[2] < .9;
    }
    void prepareTraversal() {
        if (!(total > 0) || !std::isfinite(total)
            || shapes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() / 4)) { return; }
        osci::Point previous, strokeStart;
        bool havePrevious = false;
        std::size_t previousShape = 0;
        for (std::size_t index = 0; index < shapes.size(); ++index) {
            const auto before = index == 0 ? 0.0 : ends[index - 1];
            if (!(ends[index] > before)) { continue; }
            const auto start = shapes[index]->nextVector(0);
            const auto end = shapes[index]->nextVector(1);
            if (!connected(start, start) || !connected(end, end)) {
                detailed = {};
                coarse = {};
                return;
            }
            if (!havePrevious || !connected(previous, start)) {
                if (havePrevious) { detailed.strokes.back().fades = fading && !connected(previous, strokeStart); }
                detailed.strokes.push_back({detailed.segments.size(), detailed.segments.size(), false});
                detailed.segments.push_back({index, index, 0, before, ends[index]});
                strokeStart = start;
            } else if (corner(previousShape, index)) {
                detailed.segments.push_back({index, index, 0, before, ends[index]});
            } else {
                // Joined smoothly: the run goes on through it.
                detailed.segments.back().lastShape = index;
                detailed.segments.back().lengthEnd = ends[index];
            }
            detailed.strokes.back().last = detailed.segments.size() - 1;
            previous = end;
            previousShape = index;
            havePrevious = true;
        }
        if (!havePrevious) { return; }
        detailed.strokes.back().fades = fading && !connected(previous, strokeStart);
        // Coarse: each stroke one run, end to end.
        for (const auto& stroke : detailed.strokes) {
            const auto& first = detailed.segments[stroke.first];
            const auto& last = detailed.segments[stroke.last];
            coarse.strokes.push_back({coarse.segments.size(), coarse.segments.size(), stroke.fades});
            coarse.segments.push_back({first.firstShape, last.lastShape, 0, first.lengthStart, last.lengthEnd});
        }
        // A dark start, a lit start, each run's end and a dark end.
        for (auto* traversal : {&detailed, &coarse}) {
            std::int64_t reserved = 0;
            for (const auto& stroke : traversal->strokes) {
                reserved += 2;
                for (auto segment = stroke.first; segment <= stroke.last; ++segment) { traversal->segments[segment].baseEnd = reserved++; }
                ++reserved;
            }
            traversal->minimum = reserved;
        }
    }

    static bool connected(const osci::Point& a, const osci::Point& b) {
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
    Traversal detailed, coarse;
    std::vector<std::unique_ptr<osci::Shape>> shapes;
    std::vector<double> ends;
    std::vector<double> discontinuities;
    std::size_t lastPositive = 0;
    double total = 0.0, drawn = 0.0;
};

}
