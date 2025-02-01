#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class OscirenderAudioProcessor;
class WobbleEffect : public EffectApplication {
public:
	WobbleEffect(OscirenderAudioProcessor& p);
	~WobbleEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
    OscirenderAudioProcessor& audioProcessor;
	double smoothedFrequency = 0;
};
