#pragma once
#include <JuceHeader.h>

class OscirenderAudioProcessor;
class WobbleEffect : public osci::EffectApplication {
public:
	WobbleEffect(OscirenderAudioProcessor& p);
	~WobbleEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
    OscirenderAudioProcessor& audioProcessor;
	double smoothedFrequency = 0;
};
