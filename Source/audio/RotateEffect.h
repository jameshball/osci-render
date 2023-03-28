#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class RotateEffect : public EffectApplication {
public:
	RotateEffect();
	~RotateEffect();

	Vector2 apply(int index, Vector2 input, double value, double frequency, double sampleRate) override;
};