#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class BulgeEffect : public EffectApplication {
public:
	BulgeEffect();
	~BulgeEffect();

	Vector2 apply(int index, Vector2 input, std::vector<EffectDetails> details, double frequency, double sampleRate) override;
};