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

	int sections = 2;
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

	MainComponent main{audioProcessor, pluginEditor};
	LuaComponent lua{audioProcessor, pluginEditor};
	ObjComponent obj{audioProcessor, pluginEditor};
	TxtComponent txt{audioProcessor, pluginEditor};
	EffectsComponent effects{audioProcessor, pluginEditor};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};