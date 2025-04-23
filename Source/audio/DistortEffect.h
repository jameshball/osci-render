#pragma once
#include <JuceHeader.h>

class DistortEffect : public osci::EffectApplication {
public:
	DistortEffect(bool vertical);
	~DistortEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	bool vertical;
};
