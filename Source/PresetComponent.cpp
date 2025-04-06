#include "PresetComponent.h"
#include "PluginProcessor.h" // Include processor again if needed (usually just in .h)

//==============================================================================
PresetComponent::PresetComponent(OscirenderAudioProcessor& p) : processor(p)
{
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

    // TODO: Need a way to get initial/loaded metadata to populate fields.
    // Maybe processor needs getter methods or PresetComponent becomes a ChangeListener?
}

PresetComponent::~PresetComponent()
{
    // Destructor logic if needed
}

void PresetComponent::paint (juce::Graphics& g)
{
    // Optional: Add background painting or borders
    // g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    // g.setColour (juce::Colours::grey);
    // g.drawRect (getLocalBounds(), 1);
}

void PresetComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10); // Add some padding

    auto topRow = bounds.removeFromTop(25);
    loadSceneButton.setBounds(topRow.removeFromLeft(100));
    saveSceneButton.setBounds(topRow.removeFromLeft(100).translated(105, 0));

    bounds.removeFromTop(10); // Spacing

    auto metadataRowHeight = 25;
    auto labelWidth = 80;
    auto editorWidth = bounds.getWidth() - labelWidth - 5; // 5px spacing

    auto nameRow = bounds.removeFromTop(metadataRowHeight);
    presetNameLabel.setBounds(nameRow.removeFromLeft(labelWidth));
    presetNameEditor.setBounds(nameRow.withX(nameRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5); // Spacing

    auto authorRow = bounds.removeFromTop(metadataRowHeight);
    authorLabel.setBounds(authorRow.removeFromLeft(labelWidth));
    authorEditor.setBounds(authorRow.withX(authorRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5); // Spacing

    auto collectionRow = bounds.removeFromTop(metadataRowHeight);
    collectionLabel.setBounds(collectionRow.removeFromLeft(labelWidth));
    collectionEditor.setBounds(collectionRow.withX(collectionRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5); // Spacing
    
    // Add row for Tags
    auto tagsRow = bounds.removeFromTop(metadataRowHeight);
    tagsLabel.setBounds(tagsRow.removeFromLeft(labelWidth));
    tagsEditor.setBounds(tagsRow.withX(tagsRow.getX() + 5).withWidth(editorWidth));

    bounds.removeFromTop(5); // Spacing

    // Position Notes Label (takes up one row)
    auto notesLabelRow = bounds.removeFromTop(metadataRowHeight); 
    notesLabel.setBounds(notesLabelRow.removeFromLeft(labelWidth));
    notesLabel.setJustificationType(juce::Justification::centredLeft); // Align label like others
    
    // Notes editor takes remaining space below the label
    bounds.removeFromTop(5); // Spacing below label
    notesEditor.setBounds(bounds); 
}

void PresetComponent::loadSceneClicked()
{
    sceneChooser = std::make_unique<juce::FileChooser>("Load OsciScene File",
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      "*.osscene"); // Use new extension
    auto chooserFlags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    // Use SafePointer to capture 'this' safely for the async callback
    sceneChooser->launchAsync(chooserFlags, [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& fc)
    {
        // Check if the component still exists when the callback executes
        if (safeThis == nullptr) return;
        
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            // Call loadScene and capture the returned metadata
            SceneMetadata loadedData = safeThis->processor.loadScene(file);
            
            // Update the text fields using the returned metadata
            safeThis->updateMetadataFields(loadedData.author, loadedData.collection, loadedData.presetName, loadedData.notes, loadedData.tags);

            // The DBG message is no longer a TODO
            DBG("PresetComponent: Updated metadata fields after loading scene.");
        }
    });
}

void PresetComponent::saveSceneClicked()
{
    sceneChooser = std::make_unique<juce::FileChooser>("Save OsciScene File",
                                                     juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                     "*.osscene", // Use new extension
                                                     true); // useNativeBox = true

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectFiles;

    // Use SafePointer to capture 'this' safely for the async callback
    sceneChooser->launchAsync(chooserFlags, [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& fc)
    {
        // Check if the component still exists when the callback executes
        if (safeThis == nullptr) return;
        
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            // Get metadata from TextEditors
            auto author = safeThis->authorEditor.getText();
            auto collection = safeThis->collectionEditor.getText();
            auto presetName = safeThis->presetNameEditor.getText();
            auto notes = safeThis->notesEditor.getText();
            // Get tags (as comma-separated string)
            auto tagsCsv = safeThis->tagsEditor.getText();

            // Call the processor's save function (needs updated signature)
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
