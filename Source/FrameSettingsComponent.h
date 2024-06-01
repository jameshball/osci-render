#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/DoubleTextBox.h"
#include "components/EffectComponent.h"

class OscirenderAudioProcessorEditor;
class FrameSettingsComponent : public juce::GroupComponent, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater {
public:
    FrameSettingsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~FrameSettingsComponent();

    void resized() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void update();
    void setAnimated(bool animated);
    void setImage(bool image);
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;
    
    bool animated = true;
    bool image = true;

    juce::ToggleButton animate{"Animate"};
    juce::ToggleButton sync{"BPM Sync"};
    juce::Label rateLabel{ "Framerate","Framerate"};
    juce::Label offsetLabel{ "Offset","Offset" };
    DoubleTextBox rateBox{ audioProcessor.animationRate->min, audioProcessor.animationRate->max };
    DoubleTextBox offsetBox{ audioProcessor.animationOffset->min, audioProcessor.animationRate->max };

    juce::ToggleButton invertImage{"Invert Image"};
    EffectComponent threshold{ audioProcessor, *audioProcessor.imageThreshold };
    EffectComponent stride{ audioProcessor, *audioProcessor.imageStride };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrameSettingsComponent)
};
