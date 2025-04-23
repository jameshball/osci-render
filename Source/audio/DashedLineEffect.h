#pragma once
#include <JuceHeader.h>

class DashedLineEffect : public osci::EffectApplication {
public:
	DashedLineEffect();
	~DashedLineEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	const static int MAX_BUFFER = 192000;
	std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_BUFFER);
	int dashIndex = 0;
	int bufferIndex = 0;
};
