#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

class EffectParameter : public juce::AudioProcessorParameter {
public:
	juce::String name;
	juce::String id;
	// value is not necessarily in the range [min, max] so effect applications may need to clip to a valid range
	std::atomic<double> value = 0.0;
	std::atomic<double> min = 0.0;
	std::atomic<double> max = 1.0;
	std::atomic<double> step = 0.001;
	std::atomic<bool> smoothValueChange = true;

	EffectParameter(juce::String name, juce::String id, double value, double min, double max, double step = 0.001, bool smoothValueChange = true) : name(name), id(id), value(value), min(min), max(max), step(step), smoothValueChange(smoothValueChange) {}

	// COPY CONSTRUCTOR SHOULD ONLY BE USED BEFORE
	// THE OBJECT IS USED IN MULTIPLE THREADS
	EffectParameter(const EffectParameter& other) {
        name = other.name;
        id = other.id;
        value.store(other.value.load());
		min.store(other.min.load());
		max.store(other.max.load());
		step.store(other.step.load());
		smoothValueChange.store(other.smoothValueChange.load());
    }

	juce::String getName(int maximumStringLength) const override {
		return name.substring(0, maximumStringLength);
	}

	juce::String getLabel() const override {
        return juce::String();
    }

	float getValue() const override {
		return value.load();
	}

	void setValue(float newValue) override {
        value.store(newValue);
    }

	float getDefaultValue() const override {
        return 0.0f;
    }

	int getNumSteps() const override {
        return (max.load() - min.load()) / step.load();
    }

	bool isDiscrete() const override {
        return false;
    }

	bool isBoolean() const override {
        return false;
    }

	bool isOrientationInverted() const override {
        return false;
    }

	juce::String getText(float value, int maximumStringLength) const override {
		auto string = juce::String(value, 3);
		return string.substring(0, maximumStringLength);
	}

	float getValueForText(const juce::String& text) const override {
        return text.getFloatValue();
    }

	bool isAutomatable() const override {
        return true;
    }

	bool isMetaParameter() const override {
        return false;
    }

	juce::AudioProcessorParameter::Category getCategory() const override {
        return juce::AudioProcessorParameter::genericParameter;
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