#include "RotateEffect.h"

RotateEffect::RotateEffect() {}

RotateEffect::~RotateEffect() {}

Vector2 RotateEffect::apply(int index, Vector2 input, double value, double frequency, double sampleRate) {
    input.rotate(nextPhase(value, sampleRate));
	return input;
}
