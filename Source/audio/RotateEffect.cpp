#include "RotateEffect.h"

RotateEffect::RotateEffect() {}

RotateEffect::~RotateEffect() {}

Vector2 RotateEffect::apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
    input.rotate(nextPhase(values[0], sampleRate));
	return input;
}
