#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "../DoubleTextBox.h"
#include "../effects/EffectComponent.h"
#include "../SwitchButton.h"

class OscirenderAudioProcessorEditor;

class FrameSettingsComponent : public juce::GroupComponent,
                               public juce::AudioProcessorParameter::Listener,
                               private juce::AsyncUpdater
{
public:
    FrameSettingsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~FrameSettingsComponent() override;

    void resized() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void update();
    void setAnimated(bool animated);
    void setImage(bool image);
    int getPreferredHeight() const;

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    bool animated = true;
    bool image = true;
    mutable int cachedPreferredHeight = 0;

    jux::SwitchButton sync{audioProcessor.animationSyncBPM};
    juce::Label offsetLabel{"Offset", "Offset"};
    DoubleTextBox offsetBox{audioProcessor.animationOffset->min, audioProcessor.animationOffset->max};

    jux::SwitchButton invertImage{audioProcessor.invertImage};
    EffectComponent threshold{*audioProcessor.imageThreshold};
    EffectComponent stride{*audioProcessor.imageStride};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrameSettingsComponent)
};
