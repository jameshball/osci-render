#pragma once

#include <JuceHeader.h>
#include "TimelineLookAndFeel.h"
#include "SvgButton.h"

class TimelineComponent : public juce::Component, public juce::Timer {
public:
    TimelineComponent();
    ~TimelineComponent() override;

    void resized() override;

    // Callbacks that derived classes will implement
    std::function<void(double)> onValueChange;
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void(bool)> onRepeatChanged;
    std::function<bool(void)> isActive;

    // Public interface
    void setValue(double value, juce::NotificationType notification);
    double getValue() const;
    void setPlaying(bool shouldBePlaying);
    bool isPlaying() const;
    void setRepeat(bool shouldRepeat);
    bool getRepeat() const;
    void setRange(double start, double end);

protected:
    void timerCallback() override;
    virtual void setup() {}

    TimelineLookAndFeel timelineLookAndFeel;
    juce::Slider slider;
    SvgButton playButton{"Play", BinaryData::play_svg, juce::Colours::white, juce::Colours::white};
    SvgButton pauseButton{"Pause", BinaryData::pause_svg, juce::Colours::white, juce::Colours::white};
    SvgButton stopButton{"Stop", BinaryData::stop_svg, juce::Colours::white, juce::Colours::white};
    SvgButton repeatButton{"Repeat", BinaryData::repeat_svg, juce::Colours::white, Colours::accentColor};

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};
