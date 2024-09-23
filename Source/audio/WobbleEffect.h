#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"
#include "PitchDetector.h"

class WobbleEffect : public EffectApplication {
public:
	WobbleEffect(PitchDetector& pitchDetector);
	~WobbleEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;

private:
	PitchDetector& pitchDetector;
	double smoothedFrequency = 0;
};