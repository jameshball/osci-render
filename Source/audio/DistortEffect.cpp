#include "DistortEffect.h"

DistortEffect::DistortEffect(bool vertical) : vertical(vertical) {}

DistortEffect::~DistortEffect() {}

Point DistortEffect::apply(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
	double value = values[0];
	int vertical = (int)this->vertical;
	if (index % 2 == 0) {
		input.translate((1 - vertical) * value, vertical * value, 0);
	} else {
		input.translate((1 - vertical) * -value, vertical * -value, 0);
	}
	return input;
}
