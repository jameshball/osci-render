#include "EffectApplication.h"
#include <numbers>
#include "../MathUtil.h"

void EffectApplication::resetPhase() {
	phase = 0.0;
}

double EffectApplication::nextPhase(double frequency, double sampleRate) {
    phase += 2 * std::numbers::pi * frequency / sampleRate;
    phase = MathUtil::wrapAngle(phase);

    return phase;
}
