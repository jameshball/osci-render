#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class SmoothEffect : public EffectApplication {
public:
	SmoothEffect();
	~SmoothEffect();

	Point apply(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	Point avg;
};
