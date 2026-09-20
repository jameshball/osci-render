#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include <osci_gui/osci_gui.h>
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
    void showFileMenu(juce::Point<int> screenPosition);
    void beginRenameFile(int index);
    void finishRenameFile(bool commit);
    void layoutRenameEditor();
    void removeFile(int index);

    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    // Controls
    osci::SvgButton inputEnabled{"inputEnabled", juce::String(BinaryData::microphone_svg), juce::Colours::white, juce::Colours::red, audioProcessor.inputEnabled};
    osci::SvgButton leftArrow      { "leftArrow",  juce::String(BinaryData::left_arrow_svg),  juce::Colours::white };
    osci::SvgButton rightArrow     { "rightArrow", juce::String(BinaryData::right_arrow_svg), juce::Colours::white };
    osci::SvgButton closeFileButton{ "closeFile",  juce::String(BinaryData::delete_svg),       juce::Colours::red };
    osci::SvgButton openFileButton { "openFiles", juce::String(BinaryData::plus_svg), juce::Colours::white, juce::Colours::white };
    osci::ContextMenuLabel fileLabel;
    osci::TextEditor renameEditor{"renameFile"};
    juce::Label renameExtensionLabel;
    juce::Label fileNumberLabel;
    juce::String renameExtension;
    int renameFileIndex = -1;
    bool renamingFile = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileControlsComponent)
};
