#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class BitCrushEffect : public EffectApplication {
public:
	BitCrushEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;
};
