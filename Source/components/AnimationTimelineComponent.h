#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../LookAndFeel.h"
#include "../parser/FileParser.h"
#include "SvgButton.h"

class AnimationTimelineComponent : public juce::Component, public juce::Timer
{
public:
    AnimationTimelineComponent(OscirenderAudioProcessor& processor);
    ~AnimationTimelineComponent() override;

    void resized() override;
    void update();
    void timerCallback() override;

private:
    OscirenderAudioProcessor& audioProcessor;
    
    juce::Slider slider;
    SvgButton playButton{"Play", BinaryData::play_svg, juce::Colours::white, juce::Colours::white};
    SvgButton pauseButton{"Pause", BinaryData::pause_svg, juce::Colours::white, juce::Colours::white};
    SvgButton stopButton{"Stop", BinaryData::stop_svg, juce::Colours::white, juce::Colours::white};
    SvgButton repeatButton{"Repeat", BinaryData::repeat_svg, juce::Colours::white, Colours::accentColor, audioProcessor.loopAnimation};
    
    void setup();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationTimelineComponent)
};
