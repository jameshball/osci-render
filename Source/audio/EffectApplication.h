#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

class EffectParameter : public juce::AudioProcessorParameter {
public:
	juce::String name;
	juce::String id;
	
	std::atomic<float> min = 0.0;
	std::atomic<float> max = 1.0;
	std::atomic<float> step = 0.001;
	std::atomic<bool> smoothValueChange = true;

	EffectParameter(juce::String name, juce::String id, float value, float min, float max, float step = 0.001, bool smoothValueChange = true) : name(name), id(id), value(value), min(min), max(max), step(step), smoothValueChange(smoothValueChange) {}

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
	
	// returns value in range [0, 1]
	float getNormalisedValue(float value) const {
		// clip value to valid range
        auto min = this->min.load();
        auto max = this->max.load();
		value = juce::jlimit(min, max, value);
		// normalize value to range [0, 1]
        return (value - min) / (max - min);
    }

	float getUnnormalisedValue(float value) const {
        value = juce::jlimit(0.0f, 1.0f, value);
		auto min = this->min.load();
		auto max = this->max.load();
		return min + value * (max - min);
    }

	float getValue() const override {
		return getNormalisedValue(value.load());
	}

	float getValueUnnormalised() const {
        return value.load();
    }

	void setValue(float newValue) override {
		value = getUnnormalisedValue(newValue);
    }

	void setValueUnnormalised(float newValue) {
        value = newValue;
    }

	void setUnnormalisedValueNotifyingHost(float newValue) {
        setValueNotifyingHost(getNormalisedValue(newValue));
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
		auto string = juce::String(getUnnormalisedValue(value), 3);
		return string.substring(0, maximumStringLength);
	}

	float getValueForText(const juce::String& text) const override {
        return getNormalisedValue(text.getFloatValue());
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

private:
	// value is not necessarily in the range [min, max] so effect applications may need to clip to a valid range
	std::atomic<float> value = 0.0;
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