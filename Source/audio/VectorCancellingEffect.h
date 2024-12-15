#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class VectorCancellingEffect : public EffectApplication {
public:
	VectorCancellingEffect();
	~VectorCancellingEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	int lastIndex = 0;
	double nextInvert = 0;
};
