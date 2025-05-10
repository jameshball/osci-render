#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "TimelineComponent.h"

class AnimationTimelineComponent : public TimelineComponent
{
public:
    AnimationTimelineComponent(OscirenderAudioProcessor& processor);
    ~AnimationTimelineComponent() override;

    void update();

private:
    OscirenderAudioProcessor& audioProcessor;
    void timerCallback() override;
    void setup() override;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationTimelineComponent)
};
