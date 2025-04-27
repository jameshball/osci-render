#pragma once
#include <JuceHeader.h>

class VectorCancellingEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double value = values[0];
		if (value < 0.001) {
			return input;
		}
		double frequency = 1.0 + 9.0 * value;
		if (index < lastIndex) {
			nextInvert = nextInvert - lastIndex + frequency;
		}
		lastIndex = index;
		if (index >= nextInvert) {
			nextInvert += frequency;
		} else {
			input.scale(-1, -1, 1);
		}
		return input;
	}
private:
	int lastIndex = 0;
	double nextInvert = 0;
};
