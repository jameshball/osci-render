#include "BulgeEffect.h"

BulgeEffect::BulgeEffect() {}

BulgeEffect::~BulgeEffect() {}

Point BulgeEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
    double value = values[0];
    double translatedBulge = -value + 1;

    double r = input.magnitude();
    double theta = atan2(input.y, input.x);
    double rn = pow(r, translatedBulge);

    return Point(rn * cos(theta), rn * sin(theta));
}
