#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SettingsComponent.h"
#include "MidiComponent.h"
#include "components/VolumeComponent.h"
#include "components/MainMenuBarModel.h"
#include "LookAndFeel.h"


class OscirenderAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::CodeDocument::Listener, public juce::AsyncUpdater, public juce::ChangeListener {
public:
    OscirenderAudioProcessorEditor(OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    void initialiseCodeEditors();
    void addCodeEditor(int index);
    void removeCodeEditor(int index);
    void fileUpdated(juce::String fileName);
    void handleAsyncUpdate() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void editPerspectiveFunction(bool enabled);

    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void updateTitle();

    std::atomic<bool> editingPerspective = false;

    OscirenderLookAndFeel lookAndFeel;
private:
    OscirenderAudioProcessor& audioProcessor;
    
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    MidiComponent midi{audioProcessor, *this};
    SettingsComponent settings{audioProcessor, *this};
    VolumeComponent volume{audioProcessor};
    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<juce::CodeEditorComponent>> codeEditors;
    juce::CodeEditorComponent::ColourScheme colourScheme;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;
    std::shared_ptr<juce::CodeDocument> perspectiveCodeDocument = std::make_shared<juce::CodeDocument>();
    std::shared_ptr<juce::CodeEditorComponent> perspectiveCodeEditor = std::make_shared<juce::CodeEditorComponent>(*perspectiveCodeDocument, &luaTokeniser);

    std::unique_ptr<juce::FileChooser> chooser;
    MainMenuBarModel menuBarModel{*this};
    juce::MenuBarComponent menuBar;

    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar{&layout, 1, true};

    juce::TooltipWindow tooltipWindow{this, 0};

    std::atomic<bool> updatingDocumentsWithParserLock = false;

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();
    void updateCodeEditor();

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};
