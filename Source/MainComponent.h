#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "components/VisualiserComponent.h"
#include "audio/PitchDetector.h"
#include "UGen/ugen_JuceEnvelopeComponent.h"

class OscirenderAudioProcessorEditor;
class MainComponent : public juce::GroupComponent {
public:
	MainComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~MainComponent() override;

	void resized() override;
	void paint(juce::Graphics& g) override;
	void updateFileLabel();
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

	std::unique_ptr<juce::FileChooser> chooser;
	juce::TextButton fileButton;
	juce::TextButton closeFileButton;
	juce::Label fileLabel;

	juce::TextEditor fileName;
	juce::ComboBox fileType;
	juce::TextButton createFile{"Create File"};

	VisualiserComponent visualiser{2, audioProcessor};
	juce::TextButton openOscilloscope{"Open Oscilloscope"};

	EnvelopeContainerComponent envelope;

	juce::Label frequencyLabel;
	int callbackIndex = -1;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};