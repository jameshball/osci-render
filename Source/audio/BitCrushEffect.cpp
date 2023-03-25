#include "BitCrushEffect.h"

BitCrushEffect::BitCrushEffect() {}

BitCrushEffect::~BitCrushEffect() {}

Vector2 BitCrushEffect::apply(int index, Vector2 input) {
	double crush = 3.0 * (1.0 - value);
	return Vector2(bitCrush(input.x, crush), bitCrush(input.y, crush));
}

double BitCrushEffect::bitCrush(double value, double places) {
    long factor = (long) pow(10, places);
    value = value * factor;
    long tmp = round(value);
    return (double) tmp / factor;
}
