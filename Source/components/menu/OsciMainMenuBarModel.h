#pragma once
#include <JuceHeader.h>

#include "../AboutComponent.h"
#include "MainMenuBarModel.h"
#include <osci_texture_interop/osci_texture_interop.h>

class OscirenderAudioProcessorEditor;
class OscirenderAudioProcessor;
class OsciMainMenuBarModel : public MainMenuBarModel {
public:
    OsciMainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor);
    void resetMenuItems();

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;
    std::vector<osci::texture::SourceInfo> textureInputMenuSources;
};
