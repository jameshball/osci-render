#pragma once

#include <JuceHeader.h>

#include "CommonPluginEditor.h"
#include "LookAndFeel.h"
#include "components/panels/MidiComponent.h"
#include "PluginProcessor.h"
#include "components/panels/SettingsComponent.h"
#include "components/panels/TxtComponent.h"
#include "components/timeline/AnimationTimelineController.h"
#include "components/lua/LuaDocumentationComponent.h"
#include "components/timeline/OscirenderAudioTimelineController.h"
#include "components/menu/OsciMainMenuBarModel.h"
#include "components/SplashScreenComponent.h"
#include "visualiser/VisualiserSettings.h"
#include <osci_scripting/osci_scripting.h>

class OscirenderAudioProcessorEditor : public CommonPluginEditor, public juce::AsyncUpdater, public juce::ChangeListener, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer {
public:
    OscirenderAudioProcessorEditor(OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    void dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) override;

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
    void openRecordingSettings() override;
    void showPremiumSplashScreen() override;
    void timerCallback() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void editCustomFunction(bool enabled);

private:
    void registerFileRemovedCallback();

    OscirenderAudioProcessor& audioProcessor;

public:
    const double CLOSED_PREF_SIZE = 30.0;
    const double RESIZER_BAR_SIZE = 7.0;
    static constexpr int kMenuBarHeight = 25;
    static constexpr int kMenuBarMaxWidth = 450;

    std::atomic<bool> editingCustomFunction = false;

    bool codeEditorWasVisibleBeforeEditingCustomFunction = false;

    SettingsComponent settings{audioProcessor, *this};

#if !OSCI_PREMIUM
    juce::TextButton upgradeButton{"Upgrade to premium!"};
#endif

#if OSCI_PREMIUM
    juce::Label mtsEspLabel;
#endif

    juce::ComponentAnimator codeEditorAnimator;
    LuaComponent lua{audioProcessor, *this};
    TxtComponent txtFont{audioProcessor, *this};

    SettingsWindow visualiserSettingsWindow = SettingsWindow("Visualiser Settings", visualiserSettings, 550, 500, 1500, VISUALISER_SETTINGS_HEIGHT);

    osci::LuaConsoleComponent console;

    std::vector<std::shared_ptr<osci::LuaScriptEditorModel>> codeModels;
    std::vector<std::shared_ptr<osci::LuaScriptEditorComponent>> codeEditors;
    juce::XmlTokeniser xmlTokeniser;
    juce::ShapeButton collapseButton;

    OsciMainMenuBarModel model{audioProcessor, *this};

    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar{&layout, 1, true};

    juce::StretchableLayoutManager luaLayout;
    juce::StretchableLayoutResizerBar luaResizerBar{&luaLayout, 1, false};

    void updateCodeEditor(bool binaryFile, bool shouldOpenEditor = false);
    void setCodeEditorVisible(std::optional<bool> visible);
    void commitCodeModel(osci::LuaScriptEditorModel& model);
    std::shared_ptr<osci::LuaScriptEditorModel> getVisibleLuaEditorModel() const;

    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    // Timeline controllers for osci-render (shared pointers to allow passing to timeline component)
    std::shared_ptr<AnimationTimelineController> animationTimelineController;
    std::shared_ptr<OscirenderAudioTimelineController> audioTimelineController;
    void updateTimelineController();

private:
    void showLuaDocumentation();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscirenderAudioProcessorEditor)
};
