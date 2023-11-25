#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MainComponent.h"
#include "LuaComponent.h"
#include "ObjComponent.h"
#include "TxtComponent.h"
#include "EffectsComponent.h"

class OscirenderAudioProcessorEditor;
class SettingsComponent : public juce::Component {
public:
	SettingsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);

	void resized() override;
	void fileUpdated(juce::String fileName);
	void update();
	void disableMouseRotation();

private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

	MainComponent main{audioProcessor, pluginEditor};
	LuaComponent lua{audioProcessor, pluginEditor};
	ObjComponent obj{audioProcessor, pluginEditor};
	TxtComponent txt{audioProcessor, pluginEditor};
	EffectsComponent effects{audioProcessor, pluginEditor};

	juce::StretchableLayoutManager columnLayout;
	juce::StretchableLayoutResizerBar columnResizerBar{&columnLayout, 1, true};
	juce::StretchableLayoutManager rowLayout;
	juce::StretchableLayoutResizerBar rowResizerBar{&rowLayout, 1, false};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};