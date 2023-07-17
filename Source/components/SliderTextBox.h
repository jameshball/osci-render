#pragma once
#include <JuceHeader.h>

class SliderTextBox : public juce::Slider {
 public:
    SliderTextBox() {
        double RANGE = 999999;
        double step = 0.01;
        setRange(-RANGE, RANGE, step);
        setTextBoxStyle(juce::Slider::TextBoxAbove, false, 90, getTextBoxHeight());
        setSliderStyle(juce::Slider::SliderStyle::IncDecButtons);
        setIncDecButtonsMode(juce::Slider::IncDecButtonMode::incDecButtonsDraggable_AutoDirection);
        setMouseDragSensitivity(2 * RANGE / step);
        setSliderSnapsToMousePosition(false);
        setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
        setSize(60, 20);
    }

    ~SliderTextBox() override {}
};
