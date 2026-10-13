#pragma once

#include "MotionStyle.h"
#include "../model/TimeGrid.h"

namespace motion::ui {
// A time ruler's labels, the timeline's and the Graph's: one just after each
// major tick, `step` apart, within `band`; one its right edge would cut is
// left out. `x` places a time.
template <typename X>
void paintRulerLabels(juce::Graphics& g, const TimeGrid& grid, double from, double to, double step, juce::Rectangle<int> band, X&& x) {
    const auto font = style::body();
    g.setFont(font);
    g.setColour(osci::Colours::text().withAlpha(.7f));
    grid.forEachTick(from, to, step, [&](double time) {
        const auto left = static_cast<float>(x(time)) + 5;
        const auto text = juce::String(grid.label(time, step));
        if (left + juce::TextLayout::getStringWidth(font, text) > static_cast<float>(band.getRight() - 4)) { return; }
        g.drawText(text, juce::roundToInt(left), band.getY(), 70, band.getHeight(), juce::Justification::centredLeft);
    });
}
}
