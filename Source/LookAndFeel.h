#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

class PluginLookAndFeel : public osci::LookAndFeel {
public:
    PluginLookAndFeel();

    static PluginLookAndFeel& getSharedInstance();
    static void applyPluginColours (juce::LookAndFeel& lookAndFeel);

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;
};
