#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "visualiser/VisualiserSettings.h"
#include "components/SosciMainMenuBarModel.h"
#include "components/SvgButton.h"
#include "components/VolumeComponent.h"
#include "components/DownloaderComponent.h"

class CommonPluginEditor : public juce::AudioProcessorEditor {
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
    void openRecordingSettings();
    void resetToDefault();
    void resized() override;

private:
    CommonAudioProcessor& audioProcessor;
    bool fullScreen = false;
public:
    OscirenderLookAndFeel lookAndFeel;

    juce::String appName;
    juce::String projectFileType;
    juce::String currentFileName;
    
#if OSCI_PREMIUM
    DownloaderComponent ffmpegDownloader;
    SharedTextureManager sharedTextureManager;
#endif

#if OSCI_PREMIUM
    int VISUALISER_SETTINGS_HEIGHT = 1200;
#else
    int VISUALISER_SETTINGS_HEIGHT = 700;
#endif

    VisualiserSettings visualiserSettings = VisualiserSettings(audioProcessor.visualiserParameters, 3);
    RecordingSettings recordingSettings = RecordingSettings(audioProcessor.recordingParameters);
    SettingsWindow recordingSettingsWindow = SettingsWindow("Recording Settings", recordingSettings, 330, 350, 330, 350);
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

    std::unique_ptr<juce::FileChooser> chooser;
    juce::MenuBarComponent menuBar;
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;
    juce::DropShadower tooltipDropShadow{juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, {0,0})};

    bool usingNativeMenuBar = false;

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommonPluginEditor)
};
