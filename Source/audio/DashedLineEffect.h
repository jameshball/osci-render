#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"

class DashedLineEffect : public EffectApplication {
public:
	DashedLineEffect();
	~DashedLineEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;

private:
	const static int MAX_BUFFER = 192000;
	std::vector<Point> buffer = std::vector<Point>(MAX_BUFFER);
	int dashIndex = 0;
	int bufferIndex = 0;
};