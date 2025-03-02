#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "visualiser/VisualiserSettings.h"
#include "components/SosciMainMenuBarModel.h"
#include "components/SvgButton.h"
#include "components/VolumeComponent.h"

class CommonPluginEditor : public juce::AudioProcessorEditor {
public:
    CommonPluginEditor(CommonAudioProcessor&, juce::String appName, juce::String projectFileType, int width, int height);
    ~CommonPluginEditor() override;

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
    
    juce::File applicationFolder = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile("Application Support")
#endif
        .getChildFile("osci-render");

    juce::String ffmpegFileName =
#if JUCE_WINDOWS
        "ffmpeg.exe";
#else
        "ffmpeg";
#endif
public:
    OscirenderLookAndFeel lookAndFeel;

    juce::String appName;
    juce::String projectFileType;
    juce::String currentFileName;
    
#if SOSCI_FEATURES
    SharedTextureManager sharedTextureManager;
#endif

#if SOSCI_FEATURES
    int VISUALISER_SETTINGS_HEIGHT = 1200;
#else
    int VISUALISER_SETTINGS_HEIGHT = 700;
#endif

    VisualiserSettings visualiserSettings = VisualiserSettings(audioProcessor.visualiserParameters, 3);
    RecordingSettings recordingSettings = RecordingSettings(audioProcessor.recordingParameters);
    SettingsWindow recordingSettingsWindow = SettingsWindow("Recording Settings", recordingSettings, 330, 320, 330, 320);
    VisualiserComponent visualiser{
        audioProcessor,
#if SOSCI_FEATURES
        sharedTextureManager,
#endif
        applicationFolder.getChildFile(ffmpegFileName),
        visualiserSettings,
        recordingSettings,
        nullptr,
        appName == "sosci"
    };

    VolumeComponent volume{audioProcessor};

    std::unique_ptr<juce::FileChooser> chooser;
    juce::MenuBarComponent menuBar;

    juce::TooltipWindow tooltipWindow{nullptr, 0};
    juce::DropShadower tooltipDropShadow{juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, {0,0})};

    bool usingNativeMenuBar = false;

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommonPluginEditor)
};
