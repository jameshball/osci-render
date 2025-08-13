#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class WobbleEffect : public osci::EffectApplication {
public:
	WobbleEffect(OscirenderAudioProcessor &p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        double wobblePhase = values[1] * std::numbers::pi;
        double theta = nextPhase(audioProcessor.frequency, sampleRate) + wobblePhase;
        double delta = 0.5 * values[0] * std::sin(theta);

        return input + delta;
    }

private:
    OscirenderAudioProcessor& audioProcessor;
	double smoothedFrequency = 0;
};
