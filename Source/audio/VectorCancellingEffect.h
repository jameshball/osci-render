#pragma once
#include <JuceHeader.h>

class VectorCancellingEffect : public osci::EffectApplication {
public:
	VectorCancellingEffect();
	~VectorCancellingEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
private:
	int lastIndex = 0;
	double nextInvert = 0;
};
