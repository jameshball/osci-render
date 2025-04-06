#include "PresetComponent.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

PresetComponent::PresetComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : processor(p), pluginEditor(editor)
{
    setText("Preset");

    // Setup Labels
    authorLabel.setText("Author:", juce::dontSendNotification);
    collectionLabel.setText("Collection:", juce::dontSendNotification);
    presetNameLabel.setText("Scene Name:", juce::dontSendNotification);
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
    addAndMakeVisible(loadSceneButton);
    addAndMakeVisible(saveSceneButton);

    // Setup button callbacks
    loadSceneButton.onClick = [this] { loadSceneClicked(); };
    saveSceneButton.onClick = [this] { saveSceneClicked(); };
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
    loadSceneButton.setBounds(loadButtonBounds);
    saveSceneButton.setBounds(loadButtonBounds.translated(loadButtonBounds.getWidth() + 5, 0));

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

void PresetComponent::loadSceneClicked()
{
    sceneChooser = std::make_unique<juce::FileChooser>("Load OsciScene File",
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      "*.osscene"); // Use new extension
    auto chooserFlags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    sceneChooser->launchAsync(chooserFlags, [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            SceneMetadata loadedData = safeThis->processor.loadScene(file);
            
            safeThis->updateMetadataFields(loadedData.author, loadedData.collection, loadedData.presetName, loadedData.notes, loadedData.tags);

            DBG("PresetComponent: Updated metadata fields after loading scene.");
        }
    });
}

void PresetComponent::saveSceneClicked()
{
    sceneChooser = std::make_unique<juce::FileChooser>("Save OsciScene File",
                                                     juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                     "*.osscene", // Use new extension
                                                     true);

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectFiles;

    sceneChooser->launchAsync(chooserFlags, [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& fc)
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

            safeThis->processor.saveScene(file, author, collection, presetName, notes, tagsCsv);
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
