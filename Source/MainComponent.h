#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "components/VisualiserComponent.h"

class OscirenderAudioProcessorEditor;
class MainComponent : public juce::GroupComponent {
public:
	MainComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~MainComponent() override;

	void resized() override;
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
	std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(2048);
	VisualiserProcessor visualiserProcessor{consumer, visualiser};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};