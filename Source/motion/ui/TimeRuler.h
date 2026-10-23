#pragma once

#include "MotionStyle.h"
#include "../model/TimeGrid.h"

namespace motion::ui {
// A time ruler's labels, the timeline's and the Graph's: one centred over
// each major tick, `step` apart, within `band` (one at an edge slides in to
// stay whole, but never past its own tick). `x` places a time.
template <typename X>
void paintRulerLabels(juce::Graphics& g, const TimeGrid& grid, double from, double to, double step, juce::Rectangle<int> band, X&& x) {
    g.setFont(style::body());
    g.setColour(osci::Colours::text().withAlpha(.7f));
    const auto first = static_cast<float>(band.getX() + 2), last = static_cast<float>(band.getRight() - 4);
    grid.forEachTick(from, to, step, [&](double time) {
        const auto tick = static_cast<float>(x(time));
        const auto text = juce::String(grid.label(time, step));
        const auto width = juce::TextLayout::getStringWidth(g.getCurrentFont(), text);
        const auto left = std::clamp(tick - width / 2, first, std::max(first, last - width));
        if (tick < left - 4 || tick > left + width + 4) { return; }
        g.drawText(text, juce::Rectangle<float>(left, static_cast<float>(band.getY()), width + 2, static_cast<float>(band.getHeight())), juce::Justification::centredLeft);
    });
}

// The short marks along a ruler's foot that its labels sit over.
template <typename X>
void paintRulerTicks(juce::Graphics& g, const TimeGrid& grid, double from, double to, double step, float bottom, float height, X&& x) {
    grid.forEachTick(from, to, step, [&](double time) { g.fillRect(std::round(static_cast<float>(x(time))), bottom - height, 1.0f, height); });
}
}
