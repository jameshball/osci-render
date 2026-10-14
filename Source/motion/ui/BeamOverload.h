#pragma once

#include "Sheet.h"

namespace motion::ui {
// What the status bar's Beam overloaded mark opens: why some frames are
// simplified, and the rate that would give the beam the time to draw them
// whole (none past the highest).
class BeamOverloadPopover final : public Popover {
public:
    BeamOverloadPopover(double rate, std::optional<double> higher) : Popover("More to draw than the beam has time for", higher.has_value() ? "Use " + kilohertz(*higher) : juce::String("OK"), "Raise the sample rate") {
        text.setText("Some frames are simplified to fit: their shortest strokes are left out and detail is drawn coarser. "
            + juce::String(higher.has_value() ? "A higher sample rate gives the beam more time each frame, at " + kilohertz(rate) + " now."
                : "The sample rate is as high as it goes: fewer or simpler objects, or less fill detail, would fit."), juce::dontSendNotification);
        text.setFont(style::body());
        text.setColour(juce::Label::textColourId, osci::Colours::text().withAlpha(.85f));
        text.setJustificationType(juce::Justification::topLeft);
        text.setBorderSize({});
        addAndMakeVisible(text);
        primary.onClick = [this, higher] { if (onChoose) { onChoose(higher); } };
        setSize(widthFor(width), heightFor(0, lines * Sheet::lineHeight));
    }
    // With the rate chosen, or none to just close.
    std::function<void(std::optional<double>)> onChoose;

    static juce::String kilohertz(double rate) { return juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz"; }

protected:
    void layoutBody(juce::Rectangle<int> area) override { text.setBounds(area); }

private:
    static constexpr int width = 300, lines = 4;
    juce::Label text;
};
}
