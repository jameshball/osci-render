#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "visualiser/VisualiserSettings.h"
#include "components/menu/SosciMainMenuBarModel.h"
#include <osci_gui/osci_gui.h>
#include "components/VolumeComponent.h"
#include "components/DownloaderComponent.h"
#include "components/UpdatePromptComponent.h"
#include "components/CustomTooltipWindow.h"

#if DEBUG
    #include "melatonin_inspector/melatonin_inspector.h"
#endif

class CommonPluginEditor : public juce::AudioProcessorEditor, public juce::KeyListener, private juce::Timer {
public:
    CommonPluginEditor(CommonAudioProcessor&, juce::String appName, juce::String projectFileType, int width, int height);
    ~CommonPluginEditor() override;

    void handleCommandLine(const juce::String& commandLine);
    void initialiseMenuBar(juce::MenuBarModel& menuBarModel);
    void openProject(const juce::File& file);
    void openProject();
    void saveProject();
    void saveProjectAs();
    void updateTitle();
    void fileUpdated(juce::String fileName);
    void openAudioSettings();
    void openLicenseAndUpdates();
    void refreshBetaUpdatesButton();
    virtual void openRecordingSettings();
    virtual void showPremiumSplashScreen();

    // Overlay management — any component can show/dismiss full-editor overlays
    void showOverlay(std::unique_ptr<osci::OverlayComponent> overlay);
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
    void resetToDefault();
    void toggleFullScreen();
    void resized() override;
    void parentHierarchyChanged() override;

private:
    CommonAudioProcessor& audioProcessor;
    bool fullScreen = false;
    juce::Rectangle<int> windowedBounds;
public:
    PluginLookAndFeel lookAndFeel;

    juce::String appName;
    juce::String projectFileType;
    juce::String currentFileName;

#if OSCI_PREMIUM
    DownloaderComponent ffmpegDownloader;
    SharedTextureManager sharedTextureManager;
#endif

#if OSCI_PREMIUM
    int VISUALISER_SETTINGS_HEIGHT = 1300;
#else
    int VISUALISER_SETTINGS_HEIGHT = 770;
#endif

    VisualiserSettings visualiserSettings = VisualiserSettings(audioProcessor.visualiserParameters, 3);
    RecordingSettings recordingSettings = RecordingSettings(audioProcessor.recordingParameters);
    VisualiserComponent visualiser{
        audioProcessor,
        *this,
#if OSCI_PREMIUM
        sharedTextureManager,
#endif
        audioProcessor.applicationFolder.getChildFile(audioProcessor.ffmpegFileName),
        visualiserSettings,
        recordingSettings,
        nullptr,
        appName == "sosci"
    };

    VolumeComponent volume{audioProcessor};
    juce::TextButton betaUpdatesButton { "Beta updates" };
    UpdatePromptComponent updatePrompt{audioProcessor};

    std::unique_ptr<juce::FileChooser> chooser;
    juce::MenuBarComponent menuBar;
    juce::SharedResourcePointer<CustomTooltipWindow> tooltipWindow;

    // Undo/Redo buttons shown in the menu bar area
    osci::SvgButton undoButton{
        "Undo",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M12.5,8C9.85,8 7.45,9 5.6,10.6L2,7V16H11L7.38,12.38C8.77,11.22 10.54,10.5 12.5,10.5C16.04,10.5 19.05,12.81 20.1,16L22.47,15.22C21.08,11.03 17.15,8 12.5,8Z\" /></svg>",
        juce::Colours::white
    };
    osci::SvgButton redoButton{
        "Redo",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M18.4,10.6C16.55,9 14.15,8 11.5,8C6.85,8 2.92,11.03 1.54,15.22L3.9,16C4.95,12.81 7.95,10.5 11.5,10.5C13.45,10.5 15.23,11.22 16.62,12.38L13,16H22V7L18.4,10.6Z\" /></svg>",
        juce::Colours::white
    };

    juce::Label undoLabel;

    bool usingNativeMenuBar = false;

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

#if DEBUG
    melatonin::Inspector inspector { *this, false };
#endif

    bool keyPressed(const juce::KeyPress& key) override;
    // KeyListener — catches shortcuts on the top-level component when no child has focus
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void timerCallback() override { updateUndoRedoState(); }
    void updateUndoRedoState();
    void layoutBetaUpdatesButton(juce::Rectangle<int>& topBar);

protected:
    bool handleShortcut(const juce::KeyPress& key);
    juce::Component* topLevelKeyTarget = nullptr;
    std::vector<std::unique_ptr<osci::OverlayComponent>> activeOverlays;
    bool visualiserWasVisibleBeforeOverlay = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommonPluginEditor)
};
