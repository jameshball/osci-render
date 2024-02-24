#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MainComponent.h"
#include "LuaComponent.h"
#include "PerspectiveComponent.h"
#include "TxtComponent.h"
#include "EffectsComponent.h"
#include "MidiComponent.h"

class OscirenderAudioProcessorEditor;
class SettingsComponent : public juce::Component {
public:
	SettingsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);

	void resized() override;
	void fileUpdated(juce::String fileName);
	void update();
	void mouseMove(const juce::MouseEvent& event) override;
	void mouseDown(const juce::MouseEvent& event) override;
	void paint(juce::Graphics& g) override;

private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

	MainComponent main{audioProcessor, pluginEditor};
	PerspectiveComponent perspective{audioProcessor, pluginEditor};
	TxtComponent txt{audioProcessor, pluginEditor};
	EffectsComponent effects{audioProcessor, pluginEditor};
	MidiComponent midi{audioProcessor, pluginEditor};

	juce::StretchableLayoutManager midiLayout;
	juce::StretchableLayoutResizerBar midiResizerBar{&midiLayout, 1, false};
	juce::StretchableLayoutManager mainLayout;
	juce::StretchableLayoutResizerBar mainResizerBar{&mainLayout, 1, true};

	juce::Component* toggleComponents[1] = { &midi };
	juce::StretchableLayoutManager* toggleLayouts[1] = { &midiLayout };
	double prefSizes[1] = { 300 };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};