#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h" // Include processor header

/*
    Component for handling scene loading, saving, and metadata.
*/
class PresetComponent  : public juce::Component
{
public:
    PresetComponent(OscirenderAudioProcessor& processor);
    ~PresetComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Method to update text fields after loading a scene
    void updateMetadataFields(const juce::String& author, const juce::String& collection, const juce::String& presetName, const juce::String& notes);

private:
    OscirenderAudioProcessor& processor; // Reference to the processor

    // Metadata Labels and TextEditors
    juce::Label authorLabel;
    juce::TextEditor authorEditor;
    juce::Label collectionLabel;
    juce::TextEditor collectionEditor;
    juce::Label presetNameLabel;
    juce::TextEditor presetNameEditor;
    juce::Label notesLabel;
    juce::TextEditor notesEditor; // Multi-line for notes

    // Load/Save Buttons
    juce::TextButton loadSceneButton { "Load Scene" };
    juce::TextButton saveSceneButton { "Save Scene" };

    // File Chooser
    std::unique_ptr<juce::FileChooser> sceneChooser;

    // Button Handlers
    void loadSceneClicked();
    void saveSceneClicked();


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetComponent)
};