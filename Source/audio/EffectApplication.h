#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

struct EffectDetails {
	juce::String name;
	juce::String id;
	double value;
};

class EffectApplication {
public:
	EffectApplication() {};

	virtual Vector2 apply(int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) = 0;
	
	void resetPhase();
	double nextPhase(double frequency, double sampleRate);
private:
	double phase = 0.0;
};