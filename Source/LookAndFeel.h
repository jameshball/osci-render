#pragma once

#include <JuceHeader.h>

enum ColourIds {
    groupComponentBackgroundColourId,
    groupComponentHeaderColourId,
    effectComponentBackgroundColourId,
    effectComponentHandleColourId,
    sliderThumbOutlineColourId,
};

namespace Colours {
    const juce::Colour dark{0xff424242};
    const juce::Colour darker{0xff212121};
    const juce::Colour veryDark{0xff111111};
    const juce::Colour grey{0xff555555};
    const juce::Colour accentColor{0xff00CC00};
}

class OscirenderLookAndFeel : public juce::LookAndFeel_V4 {
public:
    OscirenderLookAndFeel();

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawTickBox(juce::Graphics& g, juce::Component& component,
        float x, float y, float w, float h,
        const bool ticked,
        const bool isEnabled,
        const bool shouldDrawButtonAsHighlighted,
        const bool shouldDrawButtonAsDown) override;
    void drawGroupComponentOutline(juce::Graphics&, int w, int h, const juce::String &text, const juce::Justification&, juce::GroupComponent&) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos,
        float minSliderPos,
        float maxSliderPos,
        const juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    
};