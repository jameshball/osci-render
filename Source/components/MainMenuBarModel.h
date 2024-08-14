#pragma once
#include <JuceHeader.h>
#include "AboutComponent.h"


class OscirenderAudioProcessorEditor;
class OscirenderAudioProcessor;
class MainMenuBarModel : public juce::MenuBarModel, public juce::AudioProcessorParameter::Listener {
public:
    MainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor);
    ~MainMenuBarModel();

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;
    void menuBarActivated(bool isActive);
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;
};
