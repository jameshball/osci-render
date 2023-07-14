#include "SmoothEffect.h"

SmoothEffect::SmoothEffect() {}

SmoothEffect::~SmoothEffect() {}

Vector2 SmoothEffect::apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
    double weight = values[0];
    weight *= 0.95;
    double strength = 10;
    weight = std::log(strength * weight + 1) / std::log(strength + 1);
    // TODO: This doesn't consider the sample rate!
    leftAvg = weight * leftAvg + (1 - weight) * input.x;
    rightAvg = weight * rightAvg + (1 - weight) * input.y;
    return Vector2(leftAvg, rightAvg);
}
