#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/DoubleTextBox.h"

class OscirenderAudioProcessorEditor;
class LineArtComponent : public juce::GroupComponent, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater {
public:
    LineArtComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~LineArtComponent();

    void resized() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void update();
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    juce::ToggleButton animate{"Animate"};
    juce::ToggleButton sync{"BPM Sync"};
    juce::Label rateLabel{ "Framerate","Framerate"};
    juce::Label offsetLabel{ "Offset","Offset" };
    DoubleTextBox rateBox{ audioProcessor.animationRate->min, audioProcessor.animationRate->max };
    DoubleTextBox offsetBox{ audioProcessor.animationOffset->min, audioProcessor.animationRate->max };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LineArtComponent)
};