#pragma once

#include <JuceHeader.h>

enum ColourIds {
    groupComponentBackgroundColourId,
    groupComponentHeaderColourId,
    effectComponentBackgroundColourId,
    effectComponentHandleColourId,
    sliderThumbOutlineColourId,
    tabbedComponentBackgroundColourId,
};

namespace Colours {
    const juce::Colour dark{0xff424242};
    const juce::Colour darker{0xff212121};
    const juce::Colour veryDark{0xff111111};
    const juce::Colour grey{0xff555555};
    const juce::Colour accentColor{0xff00cc00};
}

namespace Dracula {
    const juce::Colour background{0xff282a36};
    const juce::Colour currentLine{0xff44475a};
    const juce::Colour selection{0xff44475a};
    const juce::Colour foreground{0xfff8f8f2};
    const juce::Colour comment{0xff6272a4};
    const juce::Colour cyan{0xff8be9fd};
    const juce::Colour green{0xff50fa7b};
    const juce::Colour orange{0xffffb86c};
    const juce::Colour pink{0xffff79c6};
    const juce::Colour purple{0xffbd93f9};
    const juce::Colour red{0xffff5555};
    const juce::Colour yellow{0xfff1fa8c};
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
    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent& menuBar) override;

    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme();
};
