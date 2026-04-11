#pragma once

#include <JuceHeader.h>

// Determines the modulation paradigm for a project.
//   Simple:   per-parameter LFO dropdowns + sidechain mic button (like the free version).
//   Standard: global LFO/Envelope/Random/Sidechain panels with drag-and-drop assignment.
enum class ModulationMode {
    Simple,
    Standard
};

inline juce::String modulationModeToString(ModulationMode mode) {
    return mode == ModulationMode::Simple ? "simple" : "standard";
}

inline ModulationMode modulationModeFromString(const juce::String& s) {
    return s.equalsIgnoreCase("simple") ? ModulationMode::Simple : ModulationMode::Standard;
}
