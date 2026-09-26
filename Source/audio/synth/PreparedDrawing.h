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
        for (auto& shape : shapes) {
            const auto length = static_cast<double>(shape->length());
            total += std::isfinite(length) ? std::max(0.0, length) : 0.0;
            ends.push_back(total);
        }
    }

    Point sample(double phase) const {
        if (shapes.empty() || total <= 0.0) {
            return Point(0, 0, 0, 0, 0, 0);
        }
        const auto distance = std::clamp(phase, 0.0, std::nextafter(1.0, 0.0)) * total;
        const auto boundary = std::upper_bound(ends.begin(), ends.end(), distance);
        const auto index = std::min(static_cast<std::size_t>(boundary - ends.begin()), shapes.size() - 1);
        const auto start = index == 0 ? 0.0 : ends[index - 1];
        const auto length = ends[index] - start;
        return shapes[index]->nextVector(static_cast<float>(length > 0 ? (distance - start) / length : 0.0));
    }

    bool empty() const { return shapes.empty() || total <= 0.0; }
    double length() const { return total; }

private:
    std::vector<std::unique_ptr<Shape>> shapes;
    std::vector<double> ends;
    double total = 0.0;
};

}
