#include "BulgeEffect.h"

BulgeEffect::BulgeEffect() {}

BulgeEffect::~BulgeEffect() {}

OsciPoint BulgeEffect::apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
    double value = values[0];
    double translatedBulge = -value + 1;

    double r = sqrt(pow(input.x, 2) + pow(input.y, 2));
    double theta = atan2(input.y, input.x);
    double rn = pow(r, translatedBulge);

    return OsciPoint(rn * cos(theta), rn * sin(theta), input.z);
}
