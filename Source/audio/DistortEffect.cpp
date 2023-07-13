#include "DistortEffect.h"

DistortEffect::DistortEffect(bool vertical) : vertical(vertical) {}

DistortEffect::~DistortEffect() {}

Vector2 DistortEffect::apply(int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
	double value = details[0].value;
	int vertical = (int)this->vertical;
	if (index % 2 == 0) {
		input.translate((1 - vertical) * value, vertical * value);
	} else {
		input.translate((1 - vertical) * -value, vertical * -value);
	}
	return input;
}
