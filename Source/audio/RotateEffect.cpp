#include "RotateEffect.h"

RotateEffect::RotateEffect() {}

RotateEffect::~RotateEffect() {}

Point RotateEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
    input.rotate(nextPhase(values[0], sampleRate));
	return input;
}
