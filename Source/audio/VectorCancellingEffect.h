#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class VectorCancellingEffect : public EffectApplication {
public:
	VectorCancellingEffect();
	~VectorCancellingEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;
private:
	int lastIndex = 0;
	double nextInvert = 0;
};