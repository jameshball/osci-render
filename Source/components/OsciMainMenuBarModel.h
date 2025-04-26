#pragma once
#include <JuceHeader.h>
#include "AboutComponent.h"
#include "MainMenuBarModel.h"


class OscirenderAudioProcessorEditor;
class OscirenderAudioProcessor;
class OsciMainMenuBarModel : public MainMenuBarModel {
public:
    OsciMainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor);
    void openSyphonInputDialog();
    void disconnectSyphonInput();

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;
};
