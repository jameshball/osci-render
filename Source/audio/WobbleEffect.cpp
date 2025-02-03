#include "WobbleEffect.h"
#include "../PluginProcessor.h"

WobbleEffect::WobbleEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

WobbleEffect::~WobbleEffect() {}

OsciPoint WobbleEffect::apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
    double wobblePhase = values[1] * std::numbers::pi;
    double theta = nextPhase(audioProcessor.frequency, sampleRate) + wobblePhase;
    double delta = 0.5 * values[0] * std::sin(theta);

    return input + delta;
}
