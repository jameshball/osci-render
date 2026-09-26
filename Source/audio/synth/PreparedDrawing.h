#pragma once

#include <osci_render_core/osci_render_core.h>
#include <algorithm>
#include <cmath>

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

    bool empty() const { return shapes.empty() || total <= 0.0; }
    double length() const { return total; }

private:
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
    std::vector<std::unique_ptr<Shape>> shapes;
    std::vector<double> ends;
    std::vector<double> discontinuities;
    std::size_t lastPositive = 0;
    double total = 0.0;
};

}
