#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Base class for full-editor overlay panels (semi-transparent background with
// a rounded dark panel in the centre).  Subclasses populate the content area;
// the overlay handles the backdrop, border, and dismiss callback.
class OverlayComponent : public juce::Component {
public:
    OverlayComponent() {
        setOpaque(false);
        setAlwaysOnTop(true);
        setInterceptsMouseClicks(true, true);
    }

    ~OverlayComponent() override = default;

    // Called when the user requests to close the overlay.
    std::function<void()> onDismissRequested;

    // When true, the editor won't hide the visualiser or dim the background.
    bool lightweight = false;

    void paint(juce::Graphics& g) override {
        if (lightweight) return;
        g.fillAll(juce::Colours::black.withAlpha(0.6f));

        auto cornerRadius = 12.0f;
        auto panelFloat = panelBounds.toFloat();

        g.setColour(Colours::veryDark());
        g.fillRoundedRectangle(panelFloat, cornerRadius);

        g.setColour(Colours::accentColor().withAlpha(0.6f));
        g.drawRoundedRectangle(panelFloat.reduced(0.5f), cornerRadius, 1.5f);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto horizontalMargin = juce::jmax(40, bounds.getWidth() / 6);
        auto verticalMargin = juce::jmax(40, bounds.getHeight() / 6);
        panelBounds = bounds.reduced(horizontalMargin, verticalMargin);

        resizeContent(panelBounds.reduced(28));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // Clicking outside the panel dismisses the overlay
        if (!panelBounds.contains(e.getPosition())) {
            dismiss();
        }
    }

protected:
    // Subclasses lay out their children inside contentArea.
    virtual void resizeContent(juce::Rectangle<int> contentArea) = 0;

    void dismiss() {
        if (onDismissRequested)
            onDismissRequested();
    }

    juce::Rectangle<int> panelBounds;

    // Helper shared by subclasses for configuring labels.
    static void configureLabel(juce::Label& label, const juce::Font& font,
                               juce::Justification justification) {
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setFont(font);
        label.setJustificationType(justification);
        label.setEditable(false, false, false);
        label.setInterceptsMouseClicks(false, false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayComponent)
};
