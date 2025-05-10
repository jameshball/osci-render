#pragma once
#include <JuceHeader.h>

class DistortEffect : public osci::EffectApplication {
public:
	DistortEffect(bool vertical) : vertical(vertical) {}

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double value = values[0];
		int vertical = (int)this->vertical;
		if (index % 2 == 0) {
			input.translate((1 - vertical) * value, vertical * value, 0);
		} else {
			input.translate((1 - vertical) * -value, vertical * -value, 0);
		}
		return input;
	}
private:
	bool vertical;
};
