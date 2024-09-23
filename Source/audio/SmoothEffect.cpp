#include "SmoothEffect.h"

SmoothEffect::SmoothEffect() {}

SmoothEffect::~SmoothEffect() {}

Point SmoothEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
	double weight = juce::jmax(values[0], 0.00001);
    weight *= 0.95;
    double strength = 10;
    weight = std::log(strength * weight + 1) / std::log(strength + 1);
    // TODO: This doesn't consider the sample rate!
	avg = weight * avg + (1 - weight) * input;
    
    return avg;
}

Point SmoothEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) {
    return apply(index, input, values, sampleRate);
}