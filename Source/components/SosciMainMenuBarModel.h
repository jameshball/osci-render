#pragma once
#include <JuceHeader.h>
#include "AboutComponent.h"
#include "MainMenuBarModel.h"


class SosciPluginEditor;
class SosciAudioProcessor;
class SosciMainMenuBarModel : public MainMenuBarModel {
public:
    SosciMainMenuBarModel(SosciPluginEditor& editor, SosciAudioProcessor& processor);
    void resetMenuItems();

    SosciPluginEditor& editor;
    SosciAudioProcessor& processor;
    
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    const int SUBMENU_ID = 100;
};
