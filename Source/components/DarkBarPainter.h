#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Shared paint routine for Vital-inspired dark rounded-bar controls.
namespace DarkBarPainter {

static constexpr int kLabelHeight = osci::Colours::kLabelHeight;

// Paint the common elements: content background and label strip.
inline void paintBackground(juce::Graphics& g,
                            juce::Rectangle<float> bounds,
                            juce::Rectangle<int> labelArea,
                            const juce::String& labelText,
                            bool hovering,
                            float /*cornerRadius*/ = 4.0f) {
    juce::ignoreUnused(hovering);

    float radius = osci::Colours::kPillRadius;

    if (!labelArea.isEmpty()) {
        // Content area (rounded top corners, flat bottom)
        auto contentBounds = bounds.withBottom(labelArea.toFloat().getY());
        juce::Path contentPath;
        contentPath.addRoundedRectangle(contentBounds.getX(), contentBounds.getY(),
                                        contentBounds.getWidth(), contentBounds.getHeight(),
                                        radius, radius, true, true, false, false);
        g.setColour(osci::Colours::evenDarker());
        g.fillPath(contentPath);

        // Label strip (flat top, rounded bottom corners) — same width as content
        auto labelBounds = labelArea.toFloat();
        juce::Path labelPath;
        labelPath.addRoundedRectangle(labelBounds.getX(), labelBounds.getY(),
                                       labelBounds.getWidth(), labelBounds.getHeight(),
                                       radius, radius, false, false, true, true);
        g.setColour(osci::Colours::darkerer());
        g.fillPath(labelPath);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(labelText, labelBounds, juce::Justification::centred);
    } else {
        g.setColour(osci::Colours::evenDarker());
        g.fillRoundedRectangle(bounds, radius);
    }
}

} // namespace DarkBarPainter
