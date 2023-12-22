#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class RotateEffect : public EffectApplication {
public:
	RotateEffect();
	~RotateEffect();

	Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) override;
};