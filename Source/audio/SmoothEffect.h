#pragma once
#include <JuceHeader.h>

class SmoothEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double weight = juce::jmax(values[0].load(), 0.00001);
		weight *= 0.95;
		double strength = 10;
		weight = std::log(strength * weight + 1) / std::log(strength + 1);
		weight = std::pow(weight, 48000 / sampleRate);
		avg = weight * avg + (1 - weight) * input;
		
		return avg;
	}
private:
	osci::Point avg;
};
