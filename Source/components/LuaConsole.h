#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class LuaConsole : public juce::GroupComponent {
public:
    LuaConsole();
    ~LuaConsole();

    void print(const juce::String& text);
	void clear();

    void resized() override;
private:

    juce::TextEditor console;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaConsole)
};
