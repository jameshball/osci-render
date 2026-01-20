#pragma once

#include <JuceHeader.h>
#include "TimelineLookAndFeel.h"
#include "SvgButton.h"
#include "TimelineController.h"

class TimelineComponent : public juce::Component, public juce::Timer {
public:
    TimelineComponent();
    ~TimelineComponent() override;

    void resized() override;

    // Set the controller that defines timeline behavior
    void setController(std::shared_ptr<TimelineController> controller);
    std::shared_ptr<TimelineController> getController() const { return controller; }

    // Public interface
    void setValue(double value, juce::NotificationType notification);
    double getValue() const;
    void setPlaying(bool shouldBePlaying);
    bool isPlaying() const;
    void setRepeat(bool shouldRepeat);
    bool getRepeat() const;
    void setRange(double start, double end);

private:
    void timerCallback() override;

    std::shared_ptr<TimelineController> controller;
    
    TimelineLookAndFeel timelineLookAndFeel;
    juce::Slider slider;
    SvgButton playButton{"Play", BinaryData::play_svg, juce::Colours::white, juce::Colours::white};
    SvgButton pauseButton{"Pause", BinaryData::pause_svg, juce::Colours::white, juce::Colours::white};
    SvgButton stopButton{"Stop", BinaryData::stop_svg, juce::Colours::white, juce::Colours::white};
    SvgButton repeatButton{"Repeat", BinaryData::repeat_svg, juce::Colours::white, Colours::accentColor};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};
