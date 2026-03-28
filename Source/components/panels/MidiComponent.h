#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "../SwitchButton.h"
#include "../ParameterBarComponent.h"

class OscirenderAudioProcessorEditor;
class MidiComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
public:
	MidiComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~MidiComponent() override;

	void parameterValueChanged(int parameterIndex, float newValue) override;
	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
	void handleAsyncUpdate() override;

	void resized() override;
	void paint(juce::Graphics& g) override;
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

    jux::SwitchButton midiToggle = jux::SwitchButton(audioProcessor.midiEnabled);
	ParameterBarComponent voicesBar{audioProcessor.voices, "VOICES"};
	juce::TextButton midiSettingsButton{"Audio/MIDI Settings..."};
	ParameterBarComponent tempoBar{audioProcessor.standaloneBpm, "BPM"};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiComponent)
};
