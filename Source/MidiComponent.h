#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/SwitchButton.h"

class OscirenderAudioProcessorEditor;
class MidiComponent : public juce::GroupComponent, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater, private juce::Timer {
public:
	MidiComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~MidiComponent() override;

	void parameterValueChanged(int parameterIndex, float newValue) override;
	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
	void handleAsyncUpdate() override;
	void timerCallback() override;

	void resized() override;
	void paint(juce::Graphics& g) override;
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

    jux::SwitchButton midiToggle = jux::SwitchButton(audioProcessor.midiEnabled);
	juce::Slider voicesSlider;
	juce::Label voicesLabel;
	juce::TextButton midiSettingsButton{"Audio/MIDI Settings..."};
	juce::MidiKeyboardComponent keyboard{audioProcessor.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};

	EnvelopeContainerComponent envelope;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiComponent)
};
