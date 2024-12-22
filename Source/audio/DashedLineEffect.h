#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"

class DashedLineEffect : public EffectApplication {
public:
	DashedLineEffect();
	~DashedLineEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:
	const static int MAX_BUFFER = 192000;
	std::vector<OsciPoint> buffer = std::vector<OsciPoint>(MAX_BUFFER);
	int dashIndex = 0;
	int bufferIndex = 0;
};
