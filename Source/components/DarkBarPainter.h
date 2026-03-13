#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Shared paint routine for Vital-inspired dark rounded-bar controls
// (LfoRateComponent, LfoModeComponent, etc.).
namespace DarkBarPainter {

// Paint the common elements: background, label strip, border.
// Call this first, then add component-specific rendering (value text, icons).
inline void paintBackground(juce::Graphics& g,
                            juce::Rectangle<float> bounds,
                            juce::Rectangle<int> labelArea,
                            const juce::String& labelText,
                            bool hovering,
                            float cornerRadius = 4.0f) {
    // Background
    g.setColour(Colours::veryDark);
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Label strip at bottom
    if (!labelArea.isEmpty()) {
        auto labelBounds = labelArea.toFloat();
        juce::Path labelPath;
        labelPath.addRoundedRectangle(bounds.getX(), labelBounds.getY(),
                                       bounds.getWidth(), labelBounds.getHeight(),
                                       cornerRadius, cornerRadius, false, false, true, true);
        g.saveState();
        g.reduceClipRegion(labelPath);
        g.setColour(Colours::darkBarLabel);
        g.fillRect(labelBounds);
        g.restoreState();

        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine((int)labelBounds.getY(),
                             bounds.getX() + 1.0f, bounds.getRight() - 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(labelText, labelBounds, juce::Justification::centred);
    }

    // Border
    g.setColour(juce::Colours::white.withAlpha(hovering ? 0.12f : 0.06f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

// Standard label height and layout helper.
static constexpr int kLabelHeight = 14;

} // namespace DarkBarPainter
