#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class VectorCancellingEffect : public EffectApplication {
public:
	VectorCancellingEffect();
	~VectorCancellingEffect();

	Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) override;
private:
	int lastIndex = 0;
	double nextInvert = 0;
};