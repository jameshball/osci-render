#pragma once
#include <JuceHeader.h>
#include "SliderTextBox.h"

class LabelledTextBox : public juce::Component {
 public:
     LabelledTextBox(juce::String text) {
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
