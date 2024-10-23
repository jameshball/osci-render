#include "SmoothEffect.h"

SmoothEffect::SmoothEffect() {}

SmoothEffect::~SmoothEffect() {}

OsciPoint SmoothEffect::apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
    double weight = juce::jmax(values[0].load(), 0.00001);
    weight *= 0.95;
    double strength = 10;
    weight = std::log(strength * weight + 1) / std::log(strength + 1);
    // TODO: This doesn't consider the sample rate!
	avg = weight * avg + (1 - weight) * input;
    
    return avg;
}
