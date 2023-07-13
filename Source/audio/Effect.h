#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>
#include "EffectApplication.h"

class Effect {
public:
	Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectDetails> details);
	Effect(std::function<Vector2(int, Vector2, std::vector<EffectDetails>, double)> application, std::vector<EffectDetails> details);
	Effect(std::shared_ptr<EffectApplication> effectApplication, juce::String name, juce::String id);
	Effect(juce::String name, juce::String id, double value);
	Effect(juce::String name, juce::String id);
	Effect(std::function<Vector2(int, Vector2, std::vector<EffectDetails>, double)> application, juce::String name, juce::String id, double value);

	Vector2 apply(int index, Vector2 input);
	void apply();
	double getValue(int index);
	double getValue();
	std::vector<EffectDetails> getDetails();
	void setValue(int index, double value);
	void setValue(double value);
	int getPrecedence();
	void setPrecedence(int precedence);
	juce::String getId();
	juce::String getName();

private:
	std::vector<EffectDetails> details;
	double frequency = 1.0;
	int precedence = -1;
	int sampleRate = 192000;
	std::function<Vector2(int, Vector2, std::vector<EffectDetails>, double)> application;
	
	std::shared_ptr<EffectApplication> effectApplication;
};