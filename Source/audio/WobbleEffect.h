#pragma once
#include <JuceHeader.h>

class OscirenderAudioProcessor;
class WobbleEffect : public osci::EffectApplication {
public:
	WobbleEffect(OscirenderAudioProcessor& p);
	~WobbleEffect();

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

	std::shared_ptr<osci::Effect> build() const override {
		// Cannot build without processor reference; caller should construct explicitly and wrap in osci::Effect with parameters.
		jassertfalse;
		return nullptr;
	}

private:
    OscirenderAudioProcessor& audioProcessor;
	double smoothedFrequency = 0;
};
