#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h" // Include processor header

// Forward declare the editor class
class OscirenderAudioProcessorEditor;

/*
    Component for handling preset loading, saving, and metadata.
*/
class PresetComponent  : public juce::GroupComponent
{
public:
    PresetComponent(OscirenderAudioProcessor& processor, OscirenderAudioProcessorEditor& editor);
    ~PresetComponent() override;

    void resized() override;

    // Method to update text fields after loading a preset
    void updateMetadataFields(const juce::String& author, const juce::String& collection, const juce::String& presetName, const juce::String& notes, const juce::StringArray& tags);

private:
    OscirenderAudioProcessor& processor; // Reference to the processor
    OscirenderAudioProcessorEditor& pluginEditor; // Add reference to the editor

    // Metadata Labels and TextEditors
    juce::Label authorLabel;
    juce::TextEditor authorEditor;
    juce::Label collectionLabel;
    juce::TextEditor collectionEditor;
    juce::Label presetNameLabel;
    juce::TextEditor presetNameEditor;
    juce::Label notesLabel;
    juce::TextEditor notesEditor; // Multi-line for notes

    // Add Tags UI
    juce::Label tagsLabel;
    juce::TextEditor tagsEditor;

    // Load/Save Buttons
    juce::TextButton loadPresetButton { "Load Preset" };
    juce::TextButton savePresetButton { "Save Preset" };

    // File Chooser
    std::unique_ptr<juce::FileChooser> presetChooser;

    // Button Handlers
    void loadPresetClicked();
    void savePresetClicked();


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetComponent)
};