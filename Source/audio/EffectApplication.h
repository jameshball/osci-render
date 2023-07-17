#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

struct EffectDetails {
	juce::String name;
	juce::String id;
	std::atomic<double> value = 0.0;
	std::atomic<double> min = 0.0;
	std::atomic<double> max = 1.0;
	std::atomic<double> step = 0.001;
	std::atomic<bool> smoothValueChange = true;

	EffectDetails(juce::String name, juce::String id, double value, double min, double max, double step, bool smoothValueChange) : name(name), id(id), value(value), min(min), max(max), step(step), smoothValueChange(smoothValueChange) {}

	// COPY CONSTRUCTOR SHOULD ONLY BE USED BEFORE
	// THE OBJECT IS USED IN MULTIPLE THREADS
	EffectDetails(const EffectDetails& other) {
        name = other.name;
        id = other.id;
        value.store(other.value.load());
		min.store(other.min.load());
		max.store(other.max.load());
		step.store(other.step.load());
		smoothValueChange.store(other.smoothValueChange.load());
    }
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