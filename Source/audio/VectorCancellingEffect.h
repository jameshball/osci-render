#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class VectorCancellingEffect : public EffectApplication {
public:
	VectorCancellingEffect();
	~VectorCancellingEffect();

	Point apply(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	int lastIndex = 0;
	double nextInvert = 0;
};
