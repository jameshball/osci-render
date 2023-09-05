#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class OscirenderAudioProcessorEditor;
class MidiComponent : public juce::Component {
public:
	MidiComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);

	void resized() override;
	void paint(juce::Graphics& g) override;
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

	juce::ToggleButton midiToggle{"Enable MIDI"};
	juce::MidiKeyboardState keyboardState;
	juce::MidiKeyboardComponent keyboard{keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiComponent)
};