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
	void disableMouseRotation();
	void toggleLayout(juce::StretchableLayoutManager& layout, double prefSize);
	void mouseMove(const juce::MouseEvent& event) override;
	void mouseDown(const juce::MouseEvent& event) override;
	void paint(juce::Graphics& g) override;

private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

	MainComponent main{audioProcessor, pluginEditor};
	LuaComponent lua{audioProcessor, pluginEditor};
	PerspectiveComponent perspective{audioProcessor, pluginEditor};
	TxtComponent txt{audioProcessor, pluginEditor};
	EffectsComponent effects{audioProcessor, pluginEditor};
	MidiComponent midi{audioProcessor, pluginEditor};

	const double CLOSED_PREF_SIZE = 30.0;
	const double RESIZER_BAR_SIZE = 7.0;

	juce::StretchableLayoutManager midiLayout;
	juce::StretchableLayoutResizerBar midiResizerBar{&midiLayout, 1, false};
	juce::StretchableLayoutManager mainLayout;
	juce::StretchableLayoutResizerBar mainResizerBar{&mainLayout, 1, true};
	juce::StretchableLayoutManager mainPerspectiveLayout;
	juce::StretchableLayoutResizerBar mainPerspectiveResizerBar{&mainPerspectiveLayout, 1, false};
	juce::StretchableLayoutManager effectLayout;
	juce::StretchableLayoutResizerBar effectResizerBar{&effectLayout, 1, false};

	juce::Component* toggleComponents[4] = { &midi, &perspective, &lua, &txt };
	juce::StretchableLayoutManager* toggleLayouts[4] = { &midiLayout, &mainPerspectiveLayout, &effectLayout, &effectLayout };
	double prefSizes[4] = { -0.3, -0.5, -0.4, -0.4 };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};