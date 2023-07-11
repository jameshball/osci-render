#include "RotateEffect.h"

RotateEffect::RotateEffect() {}

RotateEffect::~RotateEffect() {}

Vector2 RotateEffect::apply(int index, Vector2 input, std::vector<EffectDetails> details, double frequency, double sampleRate) {
    input.rotate(nextPhase(details[0].value, sampleRate));
	return input;
}
