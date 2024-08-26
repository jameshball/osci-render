#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class BulgeEffect : public EffectApplication {
public:
	BulgeEffect();
	~BulgeEffect();

	Point apply(int index, Point input, const std::vector<std::atomic<double>>&, double sampleRate) override;
};
