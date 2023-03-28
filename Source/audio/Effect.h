#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>
#include "EffectApplication.h"

class Effect {
public:
	Effect(std::unique_ptr<EffectApplication> effectApplication, juce::String name, juce::String id);

	Vector2 apply(int index, Vector2 input);
	double getValue();
	void setValue(double value);
	void setFrequency(double frequency);
	int getPrecedence();
	void setPrecedence(int precedence);
	juce::String getName();
	juce::String getId();
private:
	double value = 0.0;
	double frequency = 1.0;
	int precedence = -1;
	int sampleRate = 192000;
	juce::String name;
	juce::String id;
	
	std::unique_ptr<EffectApplication> effectApplication;
};