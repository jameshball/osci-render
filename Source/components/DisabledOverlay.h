#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// A transparent overlay component that dims its parent area,
// draws centred text, and forwards clicks to an onClick callback.
// Add as a child of the component you want to grey out,
// and call setBounds(getLocalBounds()) in the parent's resized().
class DisabledOverlay : public juce::Component {
public:
    DisabledOverlay() {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setAlwaysOnTop(true);
    }

    void setText(const juce::String& t) { text = t; repaint(); }
    void setSubText(const juce::String& t) { subText = t; repaint(); }

    std::function<void()> onClick;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        auto centre = bounds.getCentre();
        float rx = bounds.getWidth() * 0.4f;
        float ry = bounds.getHeight();

        juce::ColourGradient gradient(
            osci::Colours::darker(), centre.x, centre.y,
            osci::Colours::darker().withAlpha(0.0f), centre.x + rx, centre.y, true);
        g.setGradientFill(gradient);
        g.fillEllipse(centre.x - rx, centre.y - ry, rx * 2.0f, ry * 2.0f);

        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::Font(13.0f).boldened());

        if (subText.isEmpty()) {
            g.drawText(text, bounds, juce::Justification::centred, false);
        } else {
            auto textArea = bounds.withHeight(bounds.getHeight() * 0.5f);
            g.drawText(text, textArea, juce::Justification::centredBottom, false);
            g.setFont(juce::Font(11.0f));
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            auto subArea = bounds.withTop(textArea.getBottom());
            g.drawText(subText, subArea, juce::Justification::centredTop, false);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (onClick && getLocalBounds().contains(e.getPosition()))
            onClick();
    }

    // Show/hide the overlay and update alpha + enabled on sibling components.
    void setDisabledWithSiblings(bool disabled, std::initializer_list<std::reference_wrapper<juce::Component>> siblings) {
        setVisible(disabled);
        for (auto& ref : siblings) {
            ref.get().setAlpha(disabled ? 0.4f : 1.0f);
            ref.get().setEnabled(!disabled);
        }
    }

private:
    juce::String text;
    juce::String subText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DisabledOverlay)
};
