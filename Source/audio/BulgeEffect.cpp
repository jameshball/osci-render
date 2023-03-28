#include "BulgeEffect.h"

BulgeEffect::BulgeEffect() {}

BulgeEffect::~BulgeEffect() {}

Vector2 BulgeEffect::apply(int index, Vector2 input, double value, double frequency, double sampleRate) {
    double translatedBulge = -value + 1;

    double r = input.magnitude();
    double theta = atan2(input.y, input.x);
    double rn = pow(r, translatedBulge);

    return Vector2(rn * cos(theta), rn * sin(theta));
}
