#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class BitCrushEffect : public EffectApplication {
public:
	BitCrushEffect();
	~BitCrushEffect();

	Vector2 apply(int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) override;
};