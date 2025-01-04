#pragma once
#include <JuceHeader.h>
#include "AboutComponent.h"
#include "MainMenuBarModel.h"


class SosciPluginEditor;
class SosciAudioProcessor;
class SosciMainMenuBarModel : public MainMenuBarModel {
public:
    SosciMainMenuBarModel(SosciPluginEditor& editor, SosciAudioProcessor& processor);

    SosciPluginEditor& editor;
    SosciAudioProcessor& processor;
    
    const int SUBMENU_ID = 100;
};
