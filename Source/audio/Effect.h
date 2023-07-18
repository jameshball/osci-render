#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>
#include "EffectApplication.h"
#include "EffectParameter.h"
#include "BooleanParameter.h"


class Effect {
public:
	Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectParameter> parameters);
	Effect(std::shared_ptr<EffectApplication> effectApplication, EffectParameter parameter);
	Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, std::vector<EffectParameter> parameters);
	Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, EffectParameter parameter);

	Vector2 apply(int index, Vector2 input);
	void apply();
	double getValue(int index);
	double getValue();
	void setValue(int index, double value);
	void setValue(double value);
	int getPrecedence();
	void setPrecedence(int precedence);
	void addListener(int index, juce::AudioProcessorParameter::Listener* listener);
	void removeListener(int index, juce::AudioProcessorParameter::Listener* listener);
	juce::String getId();
	juce::String getName();

	std::vector<EffectParameter> parameters;
	BooleanParameter enabled;

private:
	
	juce::SpinLock listenerLock;
	std::vector<double> smoothValues;
	double frequency = 1.0;
	int precedence = -1;
	int sampleRate = 192000;
	std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application;
	
	std::shared_ptr<EffectApplication> effectApplication;
};