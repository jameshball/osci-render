#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class StereoEffect : public EffectApplication {
public:
	StereoEffect();
	~StereoEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	void initialiseBuffer(double sampleRate);

	const double bufferLength = 0.1;
	double sampleRate = -1;
	std::vector<OsciPoint> buffer;
	int head = 0;
};
