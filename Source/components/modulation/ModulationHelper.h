#pragma once

#include <JuceHeader.h>
#include "../../audio/modulation/ModAssignment.h"

// Generic helper for computing modulation info for slider display.
// Works with any modulation source type (LFO, Envelope, Random, etc.)
// by taking the assignments and value-getter as parameters.
struct ModulationHelper {
    struct Info {
        bool active = false;
        float modulatedPos = 0.0f;
        float modulatedNorm = 0.0f;  // normalised 0..1 modulated value (for rotary knobs)
        juce::Colour colour;
    };

    // Compute aggregate modulation info for a parameter from a list of assignments.
    // - assignments: all assignments for this source type
    // - paramId: the parameter to compute modulation for
    // - sl: the slider (for position/range info)
    // - getCurrentValue: given a sourceIndex, returns the current modulation output (0..1)
    // - getSourceColour: given a sourceIndex, returns the display colour
    static Info compute(
            const std::vector<ModAssignment>& assignments,
            const juce::String& paramId,
            juce::Slider& sl,
            std::function<float(int)> getCurrentValue,
            std::function<juce::Colour(int)> getSourceColour);
};
