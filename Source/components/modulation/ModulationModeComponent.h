#pragma once

#include <JuceHeader.h>

// Configuration for a generic modulation mode selector.
// Provides mode names and callbacks for get/set operations.
struct ModulationModeConfig {
    // List of (int value, display name) pairs for the popup menu
    std::vector<std::pair<int, juce::String>> modes;

    // Get/set the current mode for a given source index
    std::function<int(int)> getMode;
    std::function<void(int, int)> setMode;

    // Label text shown below the control (e.g. "MODE", "STYLE")
    juce::String labelText = "MODE";

    // Max valid source index (for clamping)
    int maxIndex = 1;
};

// Vital-inspired dark-bar selector for modulation modes.
// Layout mirrors ModulationRateComponent: value text + bottom label strip.
// Click the value area to show a popup menu of available modes.
// Generic version — works for LFO modes, Random styles, or any mode selector.
class ModulationModeComponent : public juce::Component {
public:
    ModulationModeComponent(const ModulationModeConfig& config, int sourceIndex);
    ~ModulationModeComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setSourceIndex(int index);
    int getSourceIndex() const { return sourceIndex; }

    int getMode() const { return mode; }
    void setMode(int m);

    // Replace the available modes list (e.g. to restrict when MIDI is off)
    void setModes(std::vector<std::pair<int, juce::String>> newModes);

    void syncFromProcessor();

    juce::String getDisplayText() const;

private:
    ModulationModeConfig config;
    int sourceIndex = 0;
    int mode = 0;

    juce::Rectangle<int> valueArea;
    juce::Rectangle<int> labelArea;

    void showModePopup();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationModeComponent)
};
