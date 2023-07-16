#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

struct EffectDetails {
	juce::String name;
	juce::String id;
	double value = 0.0;
	double min = 0.0;
	double max = 1.0;
	double step = 0.001;
	bool smoothValueChange = true;
};

class EffectApplication {
public:
	EffectApplication() {};

	virtual Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) = 0;
	
	void resetPhase();
	double nextPhase(double frequency, double sampleRate);
private:
	double phase = 0.0;
};