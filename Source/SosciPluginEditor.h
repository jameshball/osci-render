#pragma once

#include <JuceHeader.h>
#include "SosciPluginProcessor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "visualiser/VisualiserSettings.h"
#include "components/SosciMainMenuBarModel.h"
#include "components/SvgButton.h"

class SosciPluginEditor : public juce::AudioProcessorEditor {
public:
    SosciPluginEditor(SosciAudioProcessor&);
    ~SosciPluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void openProject();
    void saveProject();
    void saveProjectAs();
    void updateTitle();
    void openAudioSettings();
    void resetToDefault();
    void openVisualiserSettings();

private:
    SosciAudioProcessor& audioProcessor;
    
    juce::File applicationFolder = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory)
        .getChildFile("osci-render");
public:
    OscirenderLookAndFeel lookAndFeel;

    VisualiserSettings visualiserSettings = VisualiserSettings(audioProcessor.parameters, 3);
    SettingsWindow visualiserSettingsWindow = SettingsWindow("Visualiser Settings");
    VisualiserComponent visualiser{applicationFolder.getChildFile("ffmpeg"), audioProcessor.haltRecording, audioProcessor.threadManager, visualiserSettings, nullptr, true};

    std::unique_ptr<juce::FileChooser> chooser;
    SosciMainMenuBarModel menuBarModel{*this, audioProcessor};
    juce::MenuBarComponent menuBar;

    juce::TooltipWindow tooltipWindow{nullptr, 0};

    bool usingNativeMenuBar = false;

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SosciPluginEditor)
};
