#pragma once
#include <JuceHeader.h>

class SmoothEffect : public osci::EffectApplication {
public:
	SmoothEffect();
	~SmoothEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	osci::Point avg;
};
