#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "EffectApplication.h"
#include "EffectParameter.h"
#include "BooleanParameter.h"

typedef std::function<Point(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate)> EffectApplicationType;

class Effect {
public:
	Effect(std::shared_ptr<EffectApplication> effectApplication, const std::vector<EffectParameter*>& parameters);
	Effect(std::shared_ptr<EffectApplication> effectApplication, EffectParameter* parameter);
	Effect(EffectApplicationType application, const std::vector<EffectParameter*>& parameters);
	Effect(EffectApplicationType application, EffectParameter* parameter);
    Effect(const std::vector<EffectParameter*>& parameters);
    Effect(EffectParameter* parameter);

	Point apply(int index, Point input, double volume = 0.0);
	
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
	std::vector<std::atomic<double>> actualValues;
	int precedence = -1;
	int sampleRate = 192000;
	EffectApplicationType application;
	
	std::shared_ptr<EffectApplication> effectApplication;

	void animateValues(double volume);
	float nextPhase(EffectParameter* parameter);
};
