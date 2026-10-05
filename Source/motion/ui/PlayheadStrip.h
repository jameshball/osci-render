#pragma once

#include <JuceHeader.h>
#include <optional>

namespace motion {
// Repaints only the columns a vertical playhead line leaves and enters, so
// a moving playhead does not repaint the whole view (and its children).
class PlayheadStrip {
public:
    // The view's paint records the column it drew the playhead in.
    void drawn(std::optional<int> x) { painted = x; }

    void moveTo(juce::Component& view, std::optional<int> x) const {
        if (x == painted) {
            return;
        }
        for (const auto column : {painted, x}) {
            if (column.has_value()) {
                view.repaint(*column - halfWidth, 0, 2 * halfWidth + 1, view.getHeight());
            }
        }
    }

private:
    // Wide enough for the line and the timeline's head.
    static constexpr int halfWidth = 6;
    std::optional<int> painted;
};
}
