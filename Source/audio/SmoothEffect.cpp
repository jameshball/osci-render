#include "SmoothEffect.h"

SmoothEffect::SmoothEffect() {}

SmoothEffect::~SmoothEffect() {}

Vector2 SmoothEffect::apply(int index, Vector2 input, std::vector<EffectDetails> details, double frequency, double sampleRate) {
    double weight = details[0].value;
    weight *= 0.95;
    double strength = 1000000;
    weight = std::log(strength * weight + 1) / std::log(strength + 1);
    leftAvg = weight * leftAvg + (1 - weight) * input.x;
    rightAvg = weight * rightAvg + (1 - weight) * input.y;
    return Vector2(leftAvg, rightAvg);
}
