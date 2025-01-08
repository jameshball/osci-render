#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class BitCrushEffect : public EffectApplication {
public:
	BitCrushEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
};
