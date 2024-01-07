#include "RotateEffect.h"

RotateEffect::RotateEffect() {}

RotateEffect::~RotateEffect() {}

Point RotateEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
    double phase = nextPhase(values[0], sampleRate);
    input.rotate(phase, phase, 0);
	return input;
}
