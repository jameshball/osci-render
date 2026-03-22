#pragma once

#include <JuceHeader.h>
#include "DarkBarPainter.h"

// Generic dark-bar component with a label strip at the bottom
// and a content area above it. Reusable anywhere in the UI.
//
// Subclasses override getLabelText() and can either:
//   (a) Override paintContent() to custom-draw the content area, or
//   (b) Add child components — they'll be positioned in the content area.
//
// The component paints the DarkBarPainter background, label strip, and
// hover border automatically.
class LabelledBarComponent : public juce::Component {
public:
    LabelledBarComponent() {
        setRepaintsOnMouseActivity(true);
    }

    ~LabelledBarComponent() override = default;

    // --- Subclass interface ---

    virtual juce::String getLabelText() const = 0;

    // Override to draw custom content in the area above the label strip.
    // Default implementation does nothing (use child components instead).
    virtual void paintContent(juce::Graphics& g, juce::Rectangle<int> area) {
        juce::ignoreUnused(g, area);
    }

    // Override to lay out child components within the content area.
    // Default implementation does nothing.
    virtual void resizedContent(juce::Rectangle<int> area) {
        juce::ignoreUnused(area);
    }

    // --- Paint ---

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        bool hovering = isMouseOver(true);
        DarkBarPainter::paintBackground(g, bounds, labelArea, getLabelText(), hovering);
        paintContent(g, contentArea);
    }

    // --- Layout ---

    void resized() override {
        auto bounds = getLocalBounds();
        labelArea = bounds.removeFromBottom(DarkBarPainter::kLabelHeight);
        contentArea = bounds;
        resizedContent(contentArea);
    }

    juce::Rectangle<int> getContentArea() const { return contentArea; }
    juce::Rectangle<int> getLabelArea() const { return labelArea; }

protected:
    juce::Rectangle<int> contentArea;
    juce::Rectangle<int> labelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabelledBarComponent)
};
