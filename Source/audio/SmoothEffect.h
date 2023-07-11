#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class SmoothEffect : public EffectApplication {
public:
	SmoothEffect();
	~SmoothEffect();

	Vector2 apply(int index, Vector2 input, std::vector<EffectDetails> details, double frequency, double sampleRate) override;
private:
	const int MAX_WINDOW_SIZE = 2048;
	std::vector<Vector2> window;
	int windowSize = 1;
	int head = 0;
};