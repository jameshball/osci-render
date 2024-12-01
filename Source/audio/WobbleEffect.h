#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"
#include "PitchDetector.h"

class WobbleEffect : public EffectApplication {
public:
	WobbleEffect(PitchDetector& pitchDetector);
	~WobbleEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	PitchDetector& pitchDetector;
	double smoothedFrequency = 0;
};
