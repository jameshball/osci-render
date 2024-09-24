#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>

class EffectApplication {
public:
	EffectApplication() {};

	virtual Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) = 0;
	
	void resetPhase();
	double nextPhase(double frequency, double sampleRate);
private:
	double phase = 0.0;
};