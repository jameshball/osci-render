#pragma once
#include <JuceHeader.h>
#include "SliderTextBox.h"

class LabelledTextBox : public juce::Component {
 public:
     LabelledTextBox(juce::String text, double min = -999999, double max = 999999, double step = 0.01) : textBox(min, max, step) {
        addAndMakeVisible(label);
        addAndMakeVisible(textBox);
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setFont(juce::Font(14.0f));
     }

    ~LabelledTextBox() override {}

    void resized() override {
        auto bounds = getLocalBounds();
        textBox.setBounds(bounds.removeFromRight(90));
        label.setBounds(bounds);
    }

    juce::Label label;
    SliderTextBox textBox;
};
