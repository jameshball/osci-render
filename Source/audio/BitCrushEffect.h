#pragma once
#include <JuceHeader.h>

class BitCrushEffect : public osci::EffectApplication {
public:
	BitCrushEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
};
