#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class DelayEffect : public EffectApplication {
public:
	DelayEffect();
	~DelayEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	const static int MAX_DELAY = 192000 * 10;
	std::vector<OsciPoint> delayBuffer = std::vector<OsciPoint>(MAX_DELAY);
	int head = 0;
	int position = 0;
	int samplesSinceLastDelay = 0;
};
