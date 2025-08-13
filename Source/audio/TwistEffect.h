#pragma once
#include <JuceHeader.h>
#include <numbers>

class TwistEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>> &values, double sampleRate) override {
		double twistStrength = values[0] * 4 * std::numbers::pi;
		double twistTheta = twistStrength * input.y;
		input.rotate(0.0, twistTheta, 0.0);
		return input;
	}
};