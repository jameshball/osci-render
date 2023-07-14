#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class SmoothEffect : public EffectApplication {
public:
	SmoothEffect();
	~SmoothEffect();

	Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) override;
private:
	double leftAvg = 0;
	double rightAvg = 0;
};