#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"

class DelayEffect : public EffectApplication {
public:
	DelayEffect();
	~DelayEffect();

	Vector2 apply(int index, Vector2 input, std::vector<EffectDetails> details, double frequency, double sampleRate) override;

private:
	const static int MAX_DELAY = 192000 * 10;
	std::vector<Vector2> delayBuffer = std::vector<Vector2>(MAX_DELAY);
	int head = 0;
	int position = 0;
	int samplesSinceLastDelay = 0;
};