#include "VectorCancellingEffect.h"

VectorCancellingEffect::VectorCancellingEffect() {}

VectorCancellingEffect::~VectorCancellingEffect() {}

Vector2 VectorCancellingEffect::apply(int index, Vector2 input, double value, double frequency, double sampleRate) {
    if (value < 0.001) {
		return input;
    }
    frequency = 1.0 + 9.0 * value;
    if (index < lastIndex) {
        nextInvert = nextInvert - lastIndex + frequency;
    }
    lastIndex = index;
    if (index >= nextInvert) {
        nextInvert += frequency;
    } else {
        input.scale(-1, -1);
    }
    return input;
}
