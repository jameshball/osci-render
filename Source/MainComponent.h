#pragma once

#include <JuceHeader.h>
#include "EffectComponentGroup.h"
#include "PluginProcessor.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"


class MainComponent : public juce::GroupComponent {
public:
	MainComponent(OscirenderAudioProcessor&);
	~MainComponent() override;

	void resized() override;

private:
	OscirenderAudioProcessor& audioProcessor;

	std::unique_ptr<juce::FileChooser> chooser;
	juce::TextButton fileButton;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};