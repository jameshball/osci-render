#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class DistortEffect : public EffectApplication {
public:
	DistortEffect(bool vertical);
	~DistortEffect();

	Vector2 apply(int index, Vector2 input, double value, double frequency, double sampleRate) override;
private:
	bool vertical;
};