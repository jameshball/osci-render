#include "WobbleEffect.h"

WobbleEffect::WobbleEffect(PitchDetector& pitchDetector) : pitchDetector(pitchDetector) {}

WobbleEffect::~WobbleEffect() {}

Point WobbleEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
    // TODO: this doesn't consider sample rate
    smoothedFrequency = smoothedFrequency * 0.99995 + pitchDetector.frequency * 0.00005;
    double theta = nextPhase(smoothedFrequency, sampleRate);
    double delta = values[0] * std::sin(theta);    

    return input + delta;
}
