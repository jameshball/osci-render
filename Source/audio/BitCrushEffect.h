#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class BitCrushEffect : public EffectApplication {
public:
	BitCrushEffect();

	Point apply(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
};
