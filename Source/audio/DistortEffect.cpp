#include "DistortEffect.h"

DistortEffect::DistortEffect(bool vertical) : vertical(vertical) {}

DistortEffect::~DistortEffect() {}

Vector2 DistortEffect::apply(int index, Vector2 input, double value, double frequency, double sampleRate) {
	int vertical = (int)this->vertical;
	if (index % 2 == 0) {
		input.translate((1 - vertical) * value, vertical * value);
	} else {
		input.translate((1 - vertical) * -value, vertical * -value);
	}
	return input;
}
