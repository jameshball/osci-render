#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"
#include "PitchDetector.h"

class WobbleEffect : public EffectApplication {
public:
	WobbleEffect(PitchDetector& pitchDetector);
	~WobbleEffect();

	Point apply(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	PitchDetector& pitchDetector;
	double smoothedFrequency = 0;
};
