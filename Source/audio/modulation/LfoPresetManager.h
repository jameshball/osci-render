#pragma once

#include <JuceHeader.h>
#include "LfoState.h"

// Manages LFO preset files in Vital's .vitallfo JSON format.
// User presets are stored in a configurable directory.
class LfoPresetManager {
public:
    struct PresetEntry {
        juce::String name;
        juce::File file;
    };

    explicit LfoPresetManager(const juce::File& presetsDirectory);

    const juce::File& getDirectory() const { return presetsDir; }

    // List all user preset files sorted alphabetically by name.
    std::vector<PresetEntry> getUserPresets() const;

    // Save a waveform as a .vitallfo file. Returns true on success.
    bool savePreset(const juce::String& name, const LfoWaveform& waveform);

    // Load a waveform from a .vitallfo file. Returns true on success.
    bool loadPreset(const juce::File& file, LfoWaveform& waveform, juce::String& name);

    // Delete a preset file. Returns true on success.
    bool deletePreset(const juce::File& file);

    // Import a .vitallfo file (validate and copy to presets directory).
    // Returns the destination file on success, or an invalid File on failure.
    juce::File importPreset(const juce::File& sourceFile);

    // List .vitallfo files from the Vital synth user LFOs directory (if present).
    std::vector<PresetEntry> getVitalUserPresets() const;

    // Returns the Vital user LFOs directory for this platform.
    static juce::File getVitalUserLfoDirectory();

    // Convert an internal LfoWaveform to Vital's JSON representation.
    static juce::var waveformToVitalJson(const LfoWaveform& waveform, const juce::String& name);

    // Parse Vital JSON into an internal LfoWaveform. Returns true on success.
    static bool vitalJsonToWaveform(const juce::var& json, LfoWaveform& waveform, juce::String& name);

private:
    juce::File presetsDir;

    static juce::String sanitizeFilename(const juce::String& name);
};
