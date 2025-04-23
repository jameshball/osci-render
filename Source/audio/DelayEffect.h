#pragma once
#include <JuceHeader.h>

class DelayEffect : public osci::EffectApplication {
public:
	DelayEffect();
	~DelayEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	const static int MAX_DELAY = 192000 * 10;
	std::vector<osci::Point> delayBuffer = std::vector<osci::Point>(MAX_DELAY);
	int head = 0;
	int position = 0;
	int samplesSinceLastDelay = 0;
};
