#pragma once
#include <JuceHeader.h>

class StereoEffect : public osci::EffectApplication {
public:
	StereoEffect();
	~StereoEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	void initialiseBuffer(double sampleRate);

	const double bufferLength = 0.1;
	double sampleRate = -1;
	std::vector<osci::Point> buffer;
	int head = 0;
};
