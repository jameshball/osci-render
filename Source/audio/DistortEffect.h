#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class DistortEffect : public EffectApplication {
public:
	DistortEffect(bool vertical);
	~DistortEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	bool vertical;
};
