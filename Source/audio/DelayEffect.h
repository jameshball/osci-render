#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class DelayEffect : public EffectApplication {
public:
	DelayEffect();
	~DelayEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;

private:
	const static int MAX_DELAY = 192000 * 10;
	std::vector<Point> delayBuffer = std::vector<Point>(MAX_DELAY);
	int head = 0;
	int position = 0;
	int samplesSinceLastDelay = 0;
};