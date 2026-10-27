#pragma once

#include <JuceHeader.h>
#include <optional>

namespace motion::ui {
// A view's playhead: the time it shows, moved by repainting only the columns
// the line leaves and enters, so a moving playhead does not repaint the whole
// view (and its children). The view draws the line at that time's x, which
// changes only as those columns are repainted, so a partial paint (a hover, a
// progress bar) draws it exactly where a whole one would.
class PlayheadStrip {
public:
    double time() const { return shown; }

    // `x` gives a time's x in the view, none when off it.
    template <typename ToX>
    void moveTo(juce::Component& view, double next, ToX x) {
        if (next == shown) {
            return;
        }
        const auto from = x(shown), to = x(next);
        shown = next;
        for (const auto column : {from, to}) {
            if (column.has_value()) {
                view.repaint(static_cast<int>(std::floor(*column)) - halfWidth, 0, 2 * halfWidth + 2, view.getHeight());
            }
        }
    }

private:
    // Wide enough for the line and the timeline's head.
    static constexpr int halfWidth = 6;
    double shown = 0;
};
}
