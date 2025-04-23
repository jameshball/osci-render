#pragma once
#include <JuceHeader.h>

class BulgeEffect : public osci::EffectApplication {
public:
	BulgeEffect();
	~BulgeEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>&, double sampleRate) override;
};
