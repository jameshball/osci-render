#include "EffectApplication.h"
#include <numbers>

void EffectApplication::resetPhase() {
	phase = 0.0;
}

double EffectApplication::nextPhase(double frequency, double sampleRate) {
    phase += frequency / sampleRate;

    if (phase > 1) {
        phase -= 1;
    }

    return phase * 2 * std::numbers::pi;
}
