#pragma once

#include <JuceHeader.h>
#include "LfoComponent.h"

class OscirenderAudioProcessor;

// Shared helper for computing LFO modulation info for a given parameter/slider.
// Lives in the lfo/ folder to keep EffectComponent decoupled from LFO internals.
struct LfoModulationHelper {
    struct Info {
        bool active = false;
        float modulatedPos = 0.0f;
        juce::Colour colour;
    };

    static Info compute(OscirenderAudioProcessor& processor,
                        const juce::String& paramId,
                        juce::Slider& sl);
};
