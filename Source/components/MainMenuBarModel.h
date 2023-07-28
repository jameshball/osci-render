#pragma once
#include <JuceHeader.h>

class OscirenderAudioProcessorEditor;
class MainMenuBarModel : public juce::MenuBarModel {
public:
    MainMenuBarModel(OscirenderAudioProcessorEditor& editor) : editor(editor) {}
    ~MainMenuBarModel() override {}

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;
    void menuBarActivated(bool isActive);

private:
    OscirenderAudioProcessorEditor& editor;
};
