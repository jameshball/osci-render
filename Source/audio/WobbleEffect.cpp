#include "WobbleEffect.h"

WobbleEffect::WobbleEffect(PitchDetector& pitchDetector) : pitchDetector(pitchDetector) {}

WobbleEffect::~WobbleEffect() {}

Vector2 WobbleEffect::apply(int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
    // TODO: this doesn't consider sample rate
    smoothedFrequency = smoothedFrequency * 0.99995 + pitchDetector.frequency * 0.00005;
    double theta = nextPhase(smoothedFrequency, sampleRate);
    double delta = details[0].value * std::sin(theta);
    double x = input.x + delta;
    double y = input.y + delta;

    return Vector2(x, y);
}
