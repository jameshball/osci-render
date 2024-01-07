#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class RotateEffect : public EffectApplication {
public:
	RotateEffect();
	~RotateEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
};