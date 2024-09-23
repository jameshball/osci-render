#include "VectorCancellingEffect.h"

VectorCancellingEffect::VectorCancellingEffect() {}

VectorCancellingEffect::~VectorCancellingEffect() {}

Point VectorCancellingEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
    double value = values[0];
    if (value < 0.001) {
		return input;
    }
    double frequency = 1.0 + 9.0 * value;
    if (index < lastIndex) {
        nextInvert = nextInvert - lastIndex + frequency;
    }
    lastIndex = index;
    if (index >= nextInvert) {
        nextInvert += frequency;
    } else {
        input.scale(-1, -1, 1);
    }
    return input;
}

Point VectorCancellingEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) {
    return apply(index, input, values, sampleRate);
}