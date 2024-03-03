#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "SvgButton.h"
#include "../LookAndFeel.h"

class LuaConsole : public juce::GroupComponent, public juce::Timer {
public:
    LuaConsole();
    ~LuaConsole();

    void print(const std::string& text);
	void clear();
    void timerCallback() override;
    void setConsoleOpen(bool open);
    bool getConsoleOpen() { return consoleOpen; }

    void resized() override;
private:

    bool consoleOpen = false;
    juce::SpinLock lock;
    std::string buffer;
    juce::CodeDocument document;
    juce::CodeEditorComponent console = { document, nullptr };
    juce::Label emptyConsoleLabel = { "emptyConsoleLabel", "Console is empty" };
    int consoleLines = 0;

    SvgButton clearConsoleButton { "clearConsole", juce::String(BinaryData::delete_svg), juce::Colours::red };
    SvgButton pauseConsoleButton { "pauseConsole", juce::String(BinaryData::pause_svg), juce::Colours::white, Colours::accentColor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaConsole)
};
