#include "BulgeEffect.h"

BulgeEffect::BulgeEffect() {}

BulgeEffect::~BulgeEffect() {}

Point BulgeEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
    double value = values[0];
    double translatedBulge = -value + 1;

    double r = sqrt(pow(input.x, 2) + pow(input.y, 2));
    double theta = atan2(input.y, input.x);
    double rn = pow(r, translatedBulge);

    return Point(rn * cos(theta), rn * sin(theta), input.z);
}

Point BulgeEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) {
    return apply(index, input, values, sampleRate);
}