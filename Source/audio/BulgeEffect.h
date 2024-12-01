#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class BulgeEffect : public EffectApplication {
public:
	BulgeEffect();
	~BulgeEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>&, double sampleRate) override;
};
