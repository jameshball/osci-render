#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "LookAndFeel.h"
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
public:
    OscirenderLookAndFeel lookAndFeel;

    juce::String appName;
    juce::String projectFileType;
    juce::String currentFileName;

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
