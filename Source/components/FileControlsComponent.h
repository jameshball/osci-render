#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "SvgButton.h"
#include "../LookAndFeel.h"

class OscirenderAudioProcessorEditor;

// Compact toolbar grouping: Open panel, left/right file nav, current file label, and close button
class FileControlsComponent : public juce::Component {
public:
    FileControlsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called to refresh label and arrow visibility when current file changes
    void updateFileLabel();

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    // Controls
    SvgButton inputEnabled{"inputEnabled", juce::String(BinaryData::microphone_svg), juce::Colours::white, juce::Colours::red, audioProcessor.inputEnabled};
    SvgButton leftArrow      { "leftArrow",  juce::String(BinaryData::left_arrow_svg),  juce::Colours::white };
    SvgButton rightArrow     { "rightArrow", juce::String(BinaryData::right_arrow_svg), juce::Colours::white };
    SvgButton closeFileButton{ "closeFile",  juce::String(BinaryData::delete_svg),       juce::Colours::red };
    SvgButton openFileButton { "openFiles", juce::String(BinaryData::plus_svg), juce::Colours::white, juce::Colours::white };
    juce::Label fileLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileControlsComponent)
};
