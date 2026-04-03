#pragma once

#include <JuceHeader.h>
#include "RotaryKnobComponent.h"
#include "../LookAndFeel.h"

// Generic container that draws a dark rounded-rectangle background behind a
// RotaryKnobComponent with a label underneath. Reusable for any knob that
// needs a labelled card appearance (e.g. SMOOTH, DELAY, STEREO in LFO panel).
class KnobContainerComponent : public juce::Component {
public:
    KnobContainerComponent(const juce::String& labelText) : label(labelText) {
        addAndMakeVisible(knob);
    }

    RotaryKnobComponent& getKnob() { return knob; }

    // Configure the knob's range, default, suffix, and decimal places from a FloatParameter.
    void bindToParameter(osci::FloatParameter* param, double skewCentre) {
        juce::NormalisableRange<double> range((double)param->min.load(), (double)param->max.load());
        range.setSkewForCentre(skewCentre);
        knob.setNormalisableRange(range);
        knob.setDoubleClickReturnValue(true, (double)param->defaultValue.load());
        knob.setTextValueSuffix(param->getLabel());
        knob.setNumDecimalPlacesToDisplay(3);
    }

    void setLabel(const juce::String& text) { label = text; repaint(); }
    void setAccentColour(juce::Colour c) { knob.setAccentColour(c); }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();

        // Faint rounded rectangle behind the label only
        auto labelArea = bounds.removeFromBottom(Colours::kLabelHeight);
        auto labelBg = labelArea.reduced(2.0f, 0.0f);
        g.setColour(Colours::darkerer());
        g.fillRoundedRectangle(labelBg, Colours::kPillRadius);

        // Label text
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f));
        g.drawText(label, labelArea, juce::Justification::centred, false);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromBottom(Colours::kLabelHeight);
        knob.setBounds(bounds);
    }

private:
    RotaryKnobComponent knob;
    juce::String label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobContainerComponent)
};
