#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class SmoothEffect : public EffectApplication {
public:
	SmoothEffect();
	~SmoothEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	OsciPoint avg;
};
