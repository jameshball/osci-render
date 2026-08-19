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
        juce::String category; // Vital subfolder name (e.g. "User", "Third Party")
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

    // List .vitallfo files from all Vital synth LFO directories (*/LFOs, excluding Factory).
    // Caches the result; call invalidateVitalCache() to force a re-scan.
    const std::vector<PresetEntry>& getVitalUserPresets() const;

    // Force re-scan of Vital LFO directories on next access.
    void invalidateVitalCache() { vitalCacheValid = false; }

    // Returns the Vital base directory for this platform (e.g. ~/Music/Vital on macOS).
    static juce::File getVitalBaseDirectory();

    // Convert an internal LfoWaveform to Vital's JSON representation.
    static juce::var waveformToVitalJson(const LfoWaveform& waveform, const juce::String& name);

    // Parse Vital JSON into an internal LfoWaveform. Returns true on success.
    static bool vitalJsonToWaveform(const juce::var& json, LfoWaveform& waveform, juce::String& name);

private:
    juce::File presetsDir;
    mutable std::vector<PresetEntry> vitalPresetsCache;
    mutable bool vitalCacheValid = false;

    static juce::String sanitizeFilename(const juce::String& name);
};
