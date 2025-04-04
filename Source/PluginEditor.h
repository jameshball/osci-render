#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SettingsComponent.h"
#include "MidiComponent.h"
#include "components/OsciMainMenuBarModel.h"
#include "LookAndFeel.h"
#include "components/ErrorCodeEditorComponent.h"
#include "components/LuaConsole.h"
#include "visualiser/VisualiserSettings.h"
#include "CommonPluginEditor.h"

class OscirenderAudioProcessorEditor : public CommonPluginEditor, private juce::CodeDocument::Listener, public juce::AsyncUpdater, public juce::ChangeListener, public juce::FileDragAndDropTarget {
public:
    OscirenderAudioProcessorEditor(OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    bool isBinaryFile(juce::String name);
    void initialiseCodeEditors();
    void addCodeEditor(int index);
    void removeCodeEditor(int index);
    void fileUpdated(juce::String fileName, bool shouldOpenEditor = false);
    void handleAsyncUpdate() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void toggleLayout(juce::StretchableLayoutManager& layout, double prefSize);
    void openVisualiserSettings();
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void editCustomFunction(bool enabled);

private:
    void registerFileRemovedCallback();

    OscirenderAudioProcessor& audioProcessor;
public:

    const double CLOSED_PREF_SIZE = 30.0;
    const double RESIZER_BAR_SIZE = 7.0;

    std::atomic<bool> editingCustomFunction = false;

    SettingsComponent settings{audioProcessor, *this};
    
#if !SOSCI_FEATURES
    juce::TextButton upgradeButton{"Upgrade to premium!"};
#endif

    juce::ComponentAnimator codeEditorAnimator;
    LuaComponent lua{audioProcessor, *this};

    SettingsWindow visualiserSettingsWindow = SettingsWindow("Visualiser Settings", visualiserSettings, 550, 500, 550, VISUALISER_SETTINGS_HEIGHT);

    LuaConsole console;

    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<OscirenderCodeEditorComponent>> codeEditors;
    juce::CodeEditorComponent::ColourScheme colourScheme;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;
    std::shared_ptr<juce::CodeDocument> customFunctionCodeDocument = std::make_shared<juce::CodeDocument>();
    std::shared_ptr<OscirenderCodeEditorComponent> customFunctionCodeEditor = std::make_shared<OscirenderCodeEditorComponent>(*customFunctionCodeDocument, &luaTokeniser, audioProcessor, CustomEffect::UNIQUE_ID, CustomEffect::FILE_NAME);

    OsciMainMenuBarModel model{audioProcessor, *this};

    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar{&layout, 1, true};

    juce::StretchableLayoutManager luaLayout;
    juce::StretchableLayoutResizerBar luaResizerBar{&luaLayout, 1, false};

    std::atomic<bool> updatingDocumentsWithParserLock = false;

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();
    void updateCodeEditor(bool binaryFile, bool shouldOpenEditor = false);
    void setCodeEditorVisible(std::optional<bool> visible);

    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};
