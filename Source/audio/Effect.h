#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>
#include "EffectApplication.h"
#include "EffectParameter.h"
#include "BooleanParameter.h"


class Effect {
public:
	Effect(std::shared_ptr<EffectApplication> effectApplication, const std::vector<EffectParameter*>& parameters);
	Effect(std::shared_ptr<EffectApplication> effectApplication, EffectParameter* parameter);
	Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, const std::vector<EffectParameter*>& parameters);
	Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, EffectParameter* parameter);

	Vector2 apply(int index, Vector2 input);
	
	void apply();
	double getValue(int index);
	double getValue();
	double getActualValue(int index);
	double getActualValue();
	void setValue(int index, double value);
	void setValue(double value);
	int getPrecedence();
	void setPrecedence(int precedence);
	void addListener(int index, juce::AudioProcessorParameter::Listener* listener);
	void removeListener(int index, juce::AudioProcessorParameter::Listener* listener);
	void markEnableable(bool enabled);
	juce::String getId();
	juce::String getName();
	void save(juce::XmlElement* xml);
	void load(juce::XmlElement* xml);
	EffectParameter* getParameter(juce::String id);
    void updateSampleRate(int sampleRate);

	std::vector<EffectParameter*> parameters;
	BooleanParameter* enabled;

private:
	
	juce::SpinLock listenerLock;
	std::vector<double> actualValues;
	int precedence = -1;
	int sampleRate = 192000;
	std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application;
	
	std::shared_ptr<EffectApplication> effectApplication;

	void animateValues();
	float nextPhase(EffectParameter* parameter);
};
