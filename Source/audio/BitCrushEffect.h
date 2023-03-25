#pragma once
#include "Effect.h"
#include "../shape/Vector2.h"

class BitCrushEffect : public Effect {
public:
	BitCrushEffect();
	~BitCrushEffect();

	double value = 0.0;
	double frequency = 1.0;
	int precedence = -1;

	Vector2 apply(int index, Vector2 input) override;
private:
	double bitCrush(double value, double places);
};