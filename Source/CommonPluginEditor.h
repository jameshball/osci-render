#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "audio/OutputClip.h"
#include "visualiser/VisualiserSettings.h"
#include "components/menu/SosciMainMenuBarModel.h"
#include <osci_gui/osci_gui.h>
#include "components/ProductUpdateConfig.h"

#if DEBUG && JUCE_MODULE_AVAILABLE_jucewright
    #include <jucewright/jucewright.h>
#endif

class CommonPluginEditor : public juce::AudioProcessorEditor,
                           public juce::KeyListener,
                           public osci::OverlayHost {
public:
    CommonPluginEditor(CommonAudioProcessor&, juce::String appName, juce::String projectFileType, int width, int height);
    ~CommonPluginEditor() override;

    void handleCommandLine(const juce::String& commandLine);
    void initialiseMenuBar(juce::MenuBarModel& menuBarModel);
    virtual void openProject(const juce::File& file);
    virtual void openProject();
    void saveProject();
    void saveProjectAs();
    virtual void resetWindowSizeAndPosition();
    void updateTitle();
    void fileUpdated(juce::String fileName);
    void openAudioSettings();
    void openLicenseAndUpdates();
    void openFeedback();
    void refreshBetaUpdatesButton();
    virtual void openRecordingSettings();
    virtual void showPremiumSplashScreen();

    // Overlay management — any component can show/dismiss full-editor overlays
    void showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) override;
    virtual void dismissOverlay(osci::OverlayComponent* overlay,
                                std::function<void()> beforeVisualiserRestore = nullptr);

    template<typename T>
    T* findActiveOverlay() {
        for (auto& o : activeOverlays)
            if (auto* found = dynamic_cast<T*>(o.get()))
                return found;
        return nullptr;
    }

    // Offline render: input audio file -> encoded video using Recording Settings
    void renderAudioFileToVideo();
    virtual void resetToDefault();
    void toggleFullScreen();
    bool isFullScreen();
    void resized() override;
    void parentHierarchyChanged() override;

private:
    CommonAudioProcessor& audioProcessor;
    int defaultEditorWidth = 0;
    int defaultEditorHeight = 0;
    bool fullScreen = false;
    juce::Rectangle<int> windowedBounds;
public:
    PluginLookAndFeel lookAndFeel;

    juce::String appName;
    juce::String projectFileType;
    juce::String currentFileName;

#if OSCI_PREMIUM
    DownloaderComponent ffmpegDownloader;
#endif

    VisualiserSettings visualiserSettings = VisualiserSettings(audioProcessor.visualiserParameters, 3, audioProcessor.recordingParameters);
    RecordingSettings recordingSettings = RecordingSettings(audioProcessor.recordingParameters);
    VisualiserComponent visualiser{
        audioProcessor,
        *this,
        audioProcessor.applicationFolder.getChildFile(audioProcessor.ffmpegFileName),
        visualiserSettings,
        recordingSettings,
        nullptr,
        appName == "sosci"
    };

    osci::VolumeComponent volume{
        audioProcessor.threadManager,
        *audioProcessor.volumeEffect->parameters[0],
        *audioProcessor.thresholdEffect->parameters[0],
        *audioProcessor.muteParameter,
        juce::String::createStringFromData(BinaryData::volume_svg, BinaryData::volume_svgSize),
        juce::String::createStringFromData(BinaryData::mute_svg, BinaryData::mute_svgSize),
        osci::kOutputClipBypassThreshold,
        osci::kOutputClipPeakEpsilon
    };
    juce::TextButton betaUpdatesButton { "Beta updates" };
    osci::UpdatePromptComponent updatePrompt {
        audioProcessor.licenseManager, osci::makeProductUpdateConfig()
    };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::MenuBarComponent menuBar;
    juce::SharedResourcePointer<CustomTooltipWindow> tooltipWindow;

    osci::UndoRedoComponent undoRedoControls{
        audioProcessor.getUndoManager(),
        juce::String::createStringFromData(BinaryData::undo_svg, BinaryData::undo_svgSize),
        juce::String::createStringFromData(BinaryData::redo_svg, BinaryData::redo_svgSize)
    };

    bool usingNativeMenuBar = false;

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

#if DEBUG && JUCE_MODULE_AVAILABLE_jucewright
    jucewright::EnvironmentAutomation automation { *this };
#endif

    bool keyPressed(const juce::KeyPress& key) override;
    // KeyListener — catches shortcuts on the top-level component when no child has focus
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void layoutBetaUpdatesButton(juce::Rectangle<int>& topBar);

protected:
    bool handleShortcut(const juce::KeyPress& key);
    juce::Component* topLevelKeyTarget = nullptr;
    std::vector<std::unique_ptr<osci::OverlayComponent>> activeOverlays;
    bool visualiserWasVisibleBeforeOverlay = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommonPluginEditor)
};
