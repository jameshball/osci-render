#include "PresetComponent.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

PresetComponent::PresetComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : processor(p), pluginEditor(editor)
{
    setText("Preset");

    // Setup Labels
    authorLabel.setText("Author:", juce::dontSendNotification);
    collectionLabel.setText("Collection:", juce::dontSendNotification);
    presetNameLabel.setText("Preset Name:", juce::dontSendNotification);
    notesLabel.setText("Notes:", juce::dontSendNotification);
    tagsLabel.setText("Tags (CSV):", juce::dontSendNotification);

    // Setup TextEditors
    presetNameEditor.setJustification(juce::Justification::centredLeft);
    authorEditor.setJustification(juce::Justification::centredLeft);
    collectionEditor.setJustification(juce::Justification::centredLeft);
    notesEditor.setMultiLine(true);
    notesEditor.setReturnKeyStartsNewLine(true);
    tagsEditor.setJustification(juce::Justification::centredLeft);
    tagsEditor.setTooltip("Enter tags separated by commas");

    // Make components visible
    addAndMakeVisible(authorLabel);
    addAndMakeVisible(authorEditor);
    addAndMakeVisible(collectionLabel);
    addAndMakeVisible(collectionEditor);
    addAndMakeVisible(presetNameLabel);
    addAndMakeVisible(presetNameEditor);
    addAndMakeVisible(notesLabel);
    addAndMakeVisible(notesEditor);
    addAndMakeVisible(tagsLabel);
    addAndMakeVisible(tagsEditor);
    addAndMakeVisible(loadPresetButton);
    addAndMakeVisible(savePresetButton);

    // Setup button callbacks
    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };
}

PresetComponent::~PresetComponent() {}

void PresetComponent::resized()
{
    // Only layout internal components if height is sufficient (i.e., not collapsed)
    if (getHeight() <= 30)
        return; 

    auto bounds = getLocalBounds().withTrimmedTop(20).reduced(10);

    bounds.removeFromTop(5);

    auto topRow = bounds.removeFromTop(25);
    auto loadButtonBounds = topRow.removeFromLeft(100);
    loadPresetButton.setBounds(loadButtonBounds);
    savePresetButton.setBounds(loadButtonBounds.translated(loadButtonBounds.getWidth() + 5, 0));

    bounds.removeFromTop(10);

    auto metadataRowHeight = 25;
    auto labelWidth = 80;
    auto editorWidth = bounds.getWidth() - labelWidth - 5;

    auto nameRow = bounds.removeFromTop(metadataRowHeight);
    presetNameLabel.setBounds(nameRow.removeFromLeft(labelWidth));
    presetNameEditor.setBounds(nameRow.withX(nameRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5);

    auto authorRow = bounds.removeFromTop(metadataRowHeight);
    authorLabel.setBounds(authorRow.removeFromLeft(labelWidth));
    authorEditor.setBounds(authorRow.withX(authorRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5);

    auto collectionRow = bounds.removeFromTop(metadataRowHeight);
    collectionLabel.setBounds(collectionRow.removeFromLeft(labelWidth));
    collectionEditor.setBounds(collectionRow.withX(collectionRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5);
    
    // Add row for Tags
    auto tagsRow = bounds.removeFromTop(metadataRowHeight);
    tagsLabel.setBounds(tagsRow.removeFromLeft(labelWidth));
    tagsEditor.setBounds(tagsRow.withX(tagsRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5);

    // Position Notes Label (single row)
    auto notesLabelRow = bounds.removeFromTop(metadataRowHeight); 
    notesLabel.setBounds(notesLabelRow.removeFromLeft(labelWidth));
    notesLabel.setJustificationType(juce::Justification::centredLeft);
    
    // Notes editor takes remaining space below the label
    bounds.removeFromTop(5);
    notesEditor.setBounds(bounds); 
}

void PresetComponent::loadPresetClicked()
{
    presetChooser = std::make_unique<juce::FileChooser>("Load OsciPreset File",
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      "*.ospreset"); // Use new extension
    auto chooserFlags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    presetChooser->launchAsync(chooserFlags, [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            PresetMetadata loadedData = safeThis->processor.loadPreset(file);
            
            safeThis->updateMetadataFields(loadedData.author, loadedData.collection, loadedData.presetName, loadedData.notes, loadedData.tags);

            DBG("PresetComponent: Updated metadata fields after loading preset.");
        }
    });
}

void PresetComponent::savePresetClicked()
{
    presetChooser = std::make_unique<juce::FileChooser>("Save OsciPreset File",
                                                     juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                     "*.ospreset", // Use new extension
                                                     true);

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectFiles;

    presetChooser->launchAsync(chooserFlags, [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            auto author = safeThis->authorEditor.getText();
            auto collection = safeThis->collectionEditor.getText();
            auto presetName = safeThis->presetNameEditor.getText();
            auto notes = safeThis->notesEditor.getText();
            auto tagsCsv = safeThis->tagsEditor.getText();

            safeThis->processor.savePreset(file, author, collection, presetName, notes, tagsCsv);
        }
    });
}

// Method implementation to update text fields
void PresetComponent::updateMetadataFields(const juce::String& author, const juce::String& collection, const juce::String& presetName, const juce::String& notes, const juce::StringArray& tags)
{
    authorEditor.setText(author, juce::dontSendNotification);
    collectionEditor.setText(collection, juce::dontSendNotification);
    presetNameEditor.setText(presetName, juce::dontSendNotification);
    notesEditor.setText(notes, juce::dontSendNotification);
    // Join tags into comma-separated string for display
    tagsEditor.setText(tags.joinIntoString(","), juce::dontSendNotification);
}
