#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/SwitchButton.h"
#include "components/modulation/TempoComponent.h"

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
	juce::Slider voicesSlider;
	juce::Label voicesLabel;
	juce::TextButton midiSettingsButton{"Audio/MIDI Settings..."};
	TempoComponent tempoComponent;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiComponent)
};
