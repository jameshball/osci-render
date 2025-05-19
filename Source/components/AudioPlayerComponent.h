#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"
#include "../wav/WavParser.h"
#include "TimelineComponent.h"

class AudioPlayerComponent : public TimelineComponent, public AudioPlayerListener
{
public:
    AudioPlayerComponent(CommonAudioProcessor& processor);
    ~AudioPlayerComponent() override;

    void setup() override;
    void parserChanged() override;
    void setPaused(bool paused);
    bool isInitialised() const;
    void hide();
    void timerCallback() override;

    std::function<void()> onParserChanged;

private:
    CommonAudioProcessor& audioProcessor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPlayerComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE(AudioPlayerComponent)
};
