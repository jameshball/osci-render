#pragma once

#include <JuceHeader.h>
#include <vector>

// Central registry of modulation source types.
// Add new modulation types ONLY here — all UI, drag-and-drop, and paint code
// iterates this list automatically.
struct ModulationType {
    juce::String dragPrefix;      // "LFO", "ENV", "RNG" — used in drag descriptions
    juce::String propPrefix;      // "lfo", "env", "rng" — slider property key prefix
    juce::uint32 defaultColour;   // default ARGB colour for indicators
};

inline const std::vector<ModulationType>& getModulationTypes() {
    static const std::vector<ModulationType> types = {
        { "LFO", "lfo", 0xFF00E5FF },
        { "ENV", "env", 0xFFFF6E4A },
        { "RNG", "rng", 0xFF50FA7B },
    };
    return types;
}

// Drag-and-drop protocol helpers.
// Drag descriptions follow the format "MOD:TYPE:INDEX", e.g. "MOD:LFO:0".
namespace ModDrag {
    inline constexpr const char* kPrefix = "MOD:";

    inline juce::String make(const juce::String& type, int index) {
        return juce::String(kPrefix) + type + ":" + juce::String(index);
    }

    inline bool isModDrag(const juce::String& desc) {
        return desc.startsWith(kPrefix);
    }

    struct Parsed {
        bool valid = false;
        juce::String type;
        int index = 0;
    };

    inline Parsed parse(const juce::String& desc) {
        auto tokens = juce::StringArray::fromTokens(desc, ":", "");
        if (tokens.size() < 3 || tokens[0] != "MOD")
            return {};
        return { true, tokens[1], tokens[2].getIntValue() };
    }
}
