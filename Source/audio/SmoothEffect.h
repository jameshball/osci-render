#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class SmoothEffect : public EffectApplication {
public:
	SmoothEffect();
	~SmoothEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;
private:
	Point avg;
};