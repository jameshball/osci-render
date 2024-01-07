#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SettingsComponent.h"
#include "MidiComponent.h"
#include "components/VolumeComponent.h"
#include "components/MainMenuBarModel.h"
#include "LookAndFeel.h"
#include "components/ErrorCodeEditorComponent.h"


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

    void editCustomFunction(bool enabled);

    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void updateTitle();
    void openAudioSettings();
    void resetToDefault();

private:
    OscirenderAudioProcessor& audioProcessor;
public:

    OscirenderLookAndFeel lookAndFeel;

    std::atomic<bool> editingCustomFunction = false;

    VisualiserComponent visualiser{2, audioProcessor};
    std::atomic<bool> visualiserFullScreen = false;
    SettingsComponent settings{audioProcessor, *this};
    VolumeComponent volume{audioProcessor};

    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<ErrorCodeEditorComponent>> codeEditors;
    juce::CodeEditorComponent::ColourScheme colourScheme;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;
    std::shared_ptr<juce::CodeDocument> customFunctionCodeDocument = std::make_shared<juce::CodeDocument>();
    std::shared_ptr<ErrorCodeEditorComponent> customFunctionCodeEditor = std::make_shared<ErrorCodeEditorComponent>(*customFunctionCodeDocument, &luaTokeniser, audioProcessor, CustomEffect::FILE_NAME);

    std::unique_ptr<juce::FileChooser> chooser;
    MainMenuBarModel menuBarModel{*this};
    juce::MenuBarComponent menuBar;

    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar{&layout, 1, true};

    juce::TooltipWindow tooltipWindow{this, 0};

    std::atomic<bool> updatingDocumentsWithParserLock = false;

    bool usingNativeMenuBar = false;

#if !JUCE_MAC
    juce::OpenGLContext openGlContext;
#endif

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();
    void updateCodeEditor();

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};
