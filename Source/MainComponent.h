#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "visualiser/VisualiserComponent.h"
#include "audio/PitchDetector.h"
#include "UGen/ugen_JuceEnvelopeComponent.h"
#include "components/SvgButton.h"

class OscirenderAudioProcessorEditor;
class MainComponent : public juce::GroupComponent, public juce::AudioProcessorParameter::Listener {
public:
	MainComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~MainComponent() override;

	void resized() override;
	void updateFileLabel();
	void parameterValueChanged(int parameterIndex, float newValue) override;
	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;
    
    bool isBinaryFile(juce::String name);

	std::unique_ptr<juce::FileChooser> chooser;
	juce::TextButton fileButton;
	SvgButton closeFileButton{"closeFile", juce::String(BinaryData::delete_svg), juce::Colours::red};
	SvgButton inputEnabled{"inputEnabled", juce::String(BinaryData::microphone_svg), juce::Colours::white, juce::Colours::red, audioProcessor.inputEnabled};
	juce::Label fileLabel;
	SvgButton leftArrow{"leftArrow", juce::String(BinaryData::left_arrow_svg), juce::Colours::white};
	SvgButton rightArrow{"rightArrow", juce::String(BinaryData::right_arrow_svg), juce::Colours::white};
	bool showLeftArrow = false;
	bool showRightArrow = false;

	juce::TextEditor fileName;
	juce::ComboBox fileType;
	juce::TextButton createFile{"Create File"};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
