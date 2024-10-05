#pragma once
#include <JuceHeader.h>
#include "AboutComponent.h"


class SosciPluginEditor;
class SosciAudioProcessor;
class SosciMainMenuBarModel : public juce::MenuBarModel {
public:
    SosciMainMenuBarModel(SosciPluginEditor& editor, SosciAudioProcessor& processor);
    ~SosciMainMenuBarModel();

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;
    void menuBarActivated(bool isActive);

private:
    SosciPluginEditor& editor;
    SosciAudioProcessor& processor;
};
