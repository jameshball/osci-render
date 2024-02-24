#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class DistortEffect : public EffectApplication {
public:
	DistortEffect(bool vertical);
	~DistortEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
private:
	bool vertical;
};