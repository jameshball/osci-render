#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class OscirenderAudioProcessorEditor;
class LuaComponent : public juce::GroupComponent {
public:
	LuaComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~LuaComponent() override;

	void resized() override;
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;


	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaComponent)
};