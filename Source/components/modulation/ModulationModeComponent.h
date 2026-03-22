#pragma once

#include <JuceHeader.h>
#include "ModulationControlComponent.h"

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

// Click-to-select mode control built on ModulationControlComponent.
// Works for LFO modes, Random styles, or any popup-based selector.
class ModulationModeComponent : public ModulationControlComponent {
public:
    ModulationModeComponent(const ModulationModeConfig& config, int sourceIndex);
    ~ModulationModeComponent() override = default;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    juce::MouseCursor getMouseCursor() override;

    int getMode() const { return mode; }
    void setMode(int m);

    // Replace the available modes list (e.g. to restrict when MIDI is off)
    void setModes(std::vector<std::pair<int, juce::String>> newModes);

    juce::String getDisplayText() const override;
    juce::String getLabelText() const override;
    void syncFromProcessor() override;

private:
    ModulationModeConfig config;
    int mode = 0;

    void showModePopup();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationModeComponent)
};
