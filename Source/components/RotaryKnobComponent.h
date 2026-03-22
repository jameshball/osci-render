#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Rotary knob that extends juce::Slider with RotaryHorizontalVerticalDrag style.
// Custom arc + marker rendering is handled by the LookAndFeel's drawRotarySlider.
class RotaryKnobComponent : public juce::Slider {
public:
    RotaryKnobComponent() {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setPopupDisplayEnabled(true, false, nullptr);
        setColour(juce::Slider::rotarySliderFillColourId, Colours::accentColor());

        constexpr float kArcGap = juce::MathConstants<float>::pi / 3.0f;
        float startAngle = juce::MathConstants<float>::pi + kArcGap * 0.5f;
        float endAngle   = juce::MathConstants<float>::pi * 3.0f - kArcGap * 0.5f;
        setRotaryParameters({ startAngle, endAngle, true });
    }

    void setAccentColour(juce::Colour c) {
        setColour(juce::Slider::rotarySliderFillColourId, c);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnobComponent)
};
