#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

class FloatParameter : public juce::AudioProcessorParameter {
public:
	juce::String name;
	juce::String id;

	std::atomic<float> min = 0.0;
	std::atomic<float> max = 1.0;
	std::atomic<float> step = 0.001;

	FloatParameter(juce::String name, juce::String id, float value, float min, float max, float step = 0.001, juce::String label = "") : name(name), id(id), value(value), min(min), max(max), step(step), label(label) {}

	juce::String getName(int maximumStringLength) const override {
		return name.substring(0, maximumStringLength);
	}

	juce::String getLabel() const override {
		return label;
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
	juce::String label;
};

class IntParameter : public juce::AudioProcessorParameter {
public:
	juce::String name;
	juce::String id;

	std::atomic<int> min = 0;
	std::atomic<int> max = 10;

	IntParameter(juce::String name, juce::String id, int value, int min, int max) : name(name), id(id), value(value), min(min), max(max) {}

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
		value = juce::jlimit(min, max, (int) value);
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
		return 0;
	}

	int getNumSteps() const override {
		return max.load() - min.load() + 1;
	}

	bool isDiscrete() const override {
		return true;
	}

	bool isBoolean() const override {
		return false;
	}

	bool isOrientationInverted() const override {
		return false;
	}

	juce::String getText(float value, int maximumStringLength) const override {
		auto string = juce::String((int) getUnnormalisedValue(value));
		return string.substring(0, maximumStringLength);
	}

	float getValueForText(const juce::String& text) const override {
		return getNormalisedValue(text.getIntValue());
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
	std::atomic<int> value = 0;
};

enum class LfoType : int {
	Static = 1,
	Sine = 2,
	Square = 3,
	Seesaw = 4,
	Triangle = 5,
	Sawtooth = 6,
	ReverseSawtooth = 7,
	Noise = 8
};

class EffectParameter : public FloatParameter {
public:
	std::atomic<bool> smoothValueChange = true;
	IntParameter* lfo = new IntParameter(name + " LFO", id + "Lfo", 1, 1, 8);
	FloatParameter* lfoRate = new FloatParameter(name + " LFO Rate", id + "LfoRate", 1.0f, 0.0f, 100.0f, 0.1f, "Hz");

	std::vector<juce::AudioProcessorParameter*> getParameters() {
        return { this, lfo, lfoRate };
    }

	EffectParameter(juce::String name, juce::String id, float value, float min, float max, float step = 0.001, bool smoothValueChange = true) : FloatParameter(name, id, value, min, max, step), smoothValueChange(smoothValueChange) {}
};