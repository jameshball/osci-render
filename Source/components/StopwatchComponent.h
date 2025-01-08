#pragma once
#include <JuceHeader.h>

class StopwatchComponent : public juce::Component, public juce::Timer {
 public:
    StopwatchComponent() {
        addAndMakeVisible(label);
        // set mono-spaced font
        juce::Font font;
        font.setTypefaceName(juce::Font::getDefaultMonospacedFontName());
        label.setFont(font);
        label.setColour(juce::Label::textColourId, juce::Colours::red);
        label.setJustificationType(juce::Justification::right);
    }
    
    void start() {
        startTimerHz(60);
    }
    
    void addTime(juce::RelativeTime time) {
        elapsedTime += time;
    }
    
    void stop() {
        stopTimer();
    }
    
    void reset() {
        elapsedTime = juce::RelativeTime();
        timerCallback();
    }
    
    void timerCallback() override {
        int hours = elapsedTime.inHours();
        int minutes = (int) elapsedTime.inMinutes() % 60;
        int seconds = (int) elapsedTime.inSeconds() % 60;
        double millis = (int) elapsedTime.inMilliseconds() % 1000;
        label.setText(juce::String(hours).paddedLeft('0', 2) + ":" + juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(seconds).paddedLeft('0', 2) + "." + juce::String(millis).paddedLeft('0', 3), juce::dontSendNotification);
        repaint();
    }

    void resized() override {
        label.setBounds(getLocalBounds());
    }

private:

    juce::Label label;
    juce::RelativeTime elapsedTime;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StopwatchComponent)
};
