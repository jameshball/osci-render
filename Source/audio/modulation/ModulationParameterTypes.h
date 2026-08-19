#pragma once

#include <JuceHeader.h>
#include "LfoState.h"
#include "RandomState.h"

// DAW-automatable parameter types for modulation enums.
// Each wraps an osci::IntParameter with human-readable text conversion
// so DAW hosts display meaningful names instead of raw integers.

class LfoPresetParameter : public osci::IntParameter {
public:
    LfoPresetParameter(juce::String name, juce::String id, int versionHint, LfoPreset value)
        : osci::IntParameter(name, id, versionHint, (int)value, 0, juce::jmax(0, (int)getLfoPresetRegistry().size() - 1)) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        return lfoPresetToString((LfoPreset)(int)getUnnormalisedValue(value));
    }

    float getValueForText(const juce::String& text) const override {
        auto preset = stringToLfoPreset(text);
        return getNormalisedValue(preset ? (int)*preset : 0);
    }
};

class LfoModeParameter : public osci::IntParameter {
public:
    LfoModeParameter(juce::String name, juce::String id, int versionHint, LfoMode value)
        : osci::IntParameter(name, id, versionHint, (int)value, 0, (int)LfoMode::LoopHold) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        return lfoModeToString((LfoMode)(int)getUnnormalisedValue(value));
    }

    float getValueForText(const juce::String& text) const override {
        return getNormalisedValue((int)stringToLfoMode(text));
    }
};

class LfoRateModeParameter : public osci::IntParameter {
public:
    LfoRateModeParameter(juce::String name, juce::String id, int versionHint, LfoRateMode value)
        : osci::IntParameter(name, id, versionHint, (int)value, 0, (int)LfoRateMode::TempoTriplets) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        return lfoRateModeToString((LfoRateMode)(int)getUnnormalisedValue(value));
    }

    float getValueForText(const juce::String& text) const override {
        return getNormalisedValue((int)stringToLfoRateMode(text));
    }
};

class TempoDivisionParameter : public osci::IntParameter {
public:
    TempoDivisionParameter(juce::String name, juce::String id, int versionHint, int value)
        : osci::IntParameter(name, id, versionHint, value, 0, juce::jmax(0, (int)getTempoDivisions().size() - 1)) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        int idx = (int)getUnnormalisedValue(value);
        auto& divs = getTempoDivisions();
        if (idx >= 0 && idx < (int)divs.size())
            return divs[idx].toString();
        return "1/4";
    }

    float getValueForText(const juce::String& text) const override {
        auto& divs = getTempoDivisions();
        for (int i = 0; i < (int)divs.size(); ++i)
            if (divs[i].toString() == text)
                return getNormalisedValue(i);
        // Find "1/4" by name; fall back to 0 if not present
        for (int i = 0; i < (int)divs.size(); ++i)
            if (divs[i].toString() == "1/4")
                return getNormalisedValue(i);
        return getNormalisedValue(0);
    }
};

class RandomStyleParameter : public osci::IntParameter {
public:
    RandomStyleParameter(juce::String name, juce::String id, int versionHint, RandomStyle value)
        : osci::IntParameter(name, id, versionHint, (int)value, 0, (int)RandomStyle::SineInterpolate) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        return randomStyleToString((RandomStyle)(int)getUnnormalisedValue(value));
    }

    float getValueForText(const juce::String& text) const override {
        return getNormalisedValue((int)stringToRandomStyle(text));
    }
};
