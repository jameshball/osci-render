#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"
#include "PitchDetector.h"

class WobbleEffect : public EffectApplication {
public:
	WobbleEffect(PitchDetector& pitchDetector);
	~WobbleEffect();

	Vector2 apply(int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) override;

private:
	PitchDetector& pitchDetector;
	double smoothedFrequency = 0;
};