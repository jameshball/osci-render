#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SettingsComponent.h"
#include "MidiComponent.h"
#include "components/VolumeComponent.h"
#include "components/MainMenuBarModel.h"
#include "LookAndFeel.h"
#include "components/ErrorCodeEditorComponent.h"
#include "components/LuaConsole.h"

class OscirenderAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::CodeDocument::Listener, public juce::AsyncUpdater, public juce::ChangeListener {
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

    const double CLOSED_PREF_SIZE = 30.0;
    const double RESIZER_BAR_SIZE = 7.0;

    OscirenderLookAndFeel lookAndFeel;

    std::atomic<bool> editingCustomFunction = false;

    VisualiserComponent visualiser{audioProcessor, nullptr, audioProcessor.legacyVisualiserEnabled->getBoolValue()};
    std::atomic<bool> visualiserFullScreen = false;
    SettingsComponent settings{audioProcessor, *this};

    juce::ComponentAnimator codeEditorAnimator;
    LuaComponent lua{audioProcessor, *this};
    VolumeComponent volume{audioProcessor};

    LuaConsole console;

    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<OscirenderCodeEditorComponent>> codeEditors;
    juce::CodeEditorComponent::ColourScheme colourScheme;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;
    std::shared_ptr<juce::CodeDocument> customFunctionCodeDocument = std::make_shared<juce::CodeDocument>();
    std::shared_ptr<OscirenderCodeEditorComponent> customFunctionCodeEditor = std::make_shared<OscirenderCodeEditorComponent>(*customFunctionCodeDocument, &luaTokeniser, audioProcessor, CustomEffect::UNIQUE_ID, CustomEffect::FILE_NAME);

    std::unique_ptr<juce::FileChooser> chooser;
    MainMenuBarModel menuBarModel{audioProcessor, *this};
    juce::MenuBarComponent menuBar;

    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar{&layout, 1, true};

    juce::StretchableLayoutManager luaLayout;
    juce::StretchableLayoutResizerBar luaResizerBar{&luaLayout, 1, false};

    juce::TooltipWindow tooltipWindow{nullptr, 0};
    juce::DropShadower tooltipDropShadow{juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, {0,0})};

    std::atomic<bool> updatingDocumentsWithParserLock = false;

    bool usingNativeMenuBar = false;

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();
    void updateCodeEditor(bool binaryFile, bool shouldOpenEditor = false);

    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};
