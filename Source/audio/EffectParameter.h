#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

class FloatParameter : public juce::AudioProcessorParameterWithID {
public:
	std::atomic<float> min = 0.0;
	std::atomic<float> max = 0.0;
	std::atomic<float> step = 0.0;

    FloatParameter(juce::String name, juce::String id, int versionHint, float value, float min, float max, float step = 0.69, juce::String label = "") : juce::AudioProcessorParameterWithID(juce::ParameterID(id, versionHint), name), step(step), value(value), label(label) {
		// need to initialise here because of naming conflicts on Windows
		this->min = min;
		this->max = max;
	}

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

	void save(juce::XmlElement* xml) {
		xml->setAttribute("id", paramID);
		xml->setAttribute("value", value.load());
		xml->setAttribute("min", min.load());
		xml->setAttribute("max", max.load());
		xml->setAttribute("step", step.load());
	}

	// opt to not change any values if not found
	void load(juce::XmlElement* xml) {
        if (xml->hasAttribute("value")) {
			setUnnormalisedValueNotifyingHost(xml->getDoubleAttribute("value"));
        }
        if (xml->hasAttribute("min")) {
            min = xml->getDoubleAttribute("min");
        }
        if (xml->hasAttribute("max")) {
            max = xml->getDoubleAttribute("max");
        }
        if (xml->hasAttribute("step")) {
            step = xml->getDoubleAttribute("step");
        }
    }

private:
	// value is not necessarily in the range [min, max] so effect applications may need to clip to a valid range
	std::atomic<float> value = 0.0;
	juce::String label;
};

class IntParameter : public juce::AudioProcessorParameterWithID {
public:
	std::atomic<int> min = 0;
	std::atomic<int> max = 10;

    IntParameter(juce::String name, juce::String id, int versionHint, int value, int min, int max) : AudioProcessorParameterWithID(juce::ParameterID(id, versionHint), name), value(value) {
		// need to initialise here because of naming conflicts on Windows
		this->min = min;
		this->max = max;
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

	void save(juce::XmlElement* xml) {
        xml->setAttribute("id", paramID);
        xml->setAttribute("value", value.load());
        xml->setAttribute("min", min.load());
        xml->setAttribute("max", max.load());
    }

	// opt to not change any values if not found
	void load(juce::XmlElement* xml) {
		if (xml->hasAttribute("value")) {
            setUnnormalisedValueNotifyingHost(xml->getIntAttribute("value"));
        }
		if (xml->hasAttribute("min")) {
            min = xml->getIntAttribute("min");
        }
		if (xml->hasAttribute("max")) {
            max = xml->getIntAttribute("max");
        }
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

class LfoTypeParameter : public IntParameter {
public:
	LfoTypeParameter(juce::String name, juce::String id, int versionHint, int value) : IntParameter(name, id, versionHint, value, 1, 8) {}

	juce::String getText(float value, int maximumStringLength) const override {
		switch ((LfoType)(int)getUnnormalisedValue(value)) {
            case LfoType::Static:
                return "Static";
            case LfoType::Sine:
                return "Sine";
            case LfoType::Square:
                return "Square";
            case LfoType::Seesaw:
                return "Seesaw";
            case LfoType::Triangle:
                return "Triangle";
            case LfoType::Sawtooth:
                return "Sawtooth";
            case LfoType::ReverseSawtooth:
                return "Reverse Sawtooth";
            case LfoType::Noise:
                return "Noise";
            default:
                return "Unknown";
        }
	}

	float getValueForText(const juce::String& text) const override {
		int unnormalisedValue;
		if (text == "Static") {
			unnormalisedValue = (int)LfoType::Static;
		} else if (text == "Sine") {
			unnormalisedValue = (int)LfoType::Sine;
		} else if (text == "Square") {
			unnormalisedValue = (int)LfoType::Square;
		} else if (text == "Seesaw") {
			unnormalisedValue = (int)LfoType::Seesaw;
		} else if (text == "Triangle") {
			unnormalisedValue = (int)LfoType::Triangle;
		} else if (text == "Sawtooth") {
			unnormalisedValue = (int)LfoType::Sawtooth;
		} else if (text == "Reverse Sawtooth") {
			unnormalisedValue = (int)LfoType::ReverseSawtooth;
		} else if (text == "Noise") {
			unnormalisedValue = (int)LfoType::Noise;
		} else {
			unnormalisedValue = (int)LfoType::Static;
        }
		return getNormalisedValue(unnormalisedValue);
	}

	void save(juce::XmlElement* xml) {
        xml->setAttribute("lfo", getText(getValue(), 100));
    }

	void load(juce::XmlElement* xml) {
        setValueNotifyingHost(getValueForText(xml->getStringAttribute("lfo")));
    }
};

class EffectParameter : public FloatParameter {
public:
	std::atomic<bool> smoothValueChange = true;
	LfoTypeParameter* lfo = new LfoTypeParameter(name + " LFO", paramID + "Lfo", getVersionHint(), 1);
	FloatParameter* lfoRate = new FloatParameter(name + " LFO Rate", paramID + "LfoRate", getVersionHint(), 1.0f, 0.0f, 100.0f, 0.1f, "Hz");
	std::atomic<float> phase = 0.0f;
	juce::String description;

	std::vector<juce::AudioProcessorParameter*> getParameters() {
		std::vector<juce::AudioProcessorParameter*> parameters;
		parameters.push_back(this);
		if (lfo != nullptr) {
			parameters.push_back(lfo);
		}
		if (lfoRate != nullptr) {
			parameters.push_back(lfoRate);
		}
		return parameters;
    }

	void disableLfo() {
		delete lfo;
		delete lfoRate;
		lfo = nullptr;
		lfoRate = nullptr;
	}

	void save(juce::XmlElement* xml) {
		FloatParameter::save(xml);

		if (lfo != nullptr && lfoRate != nullptr) {
			auto lfoXml = xml->createNewChildElement("lfo");
			lfo->save(lfoXml);
			lfoRate->save(lfoXml);
		}
    }

	void load(juce::XmlElement* xml) {
        FloatParameter::load(xml);

        auto lfoXml = xml->getChildByName("lfo");
        if (lfoXml != nullptr) {
            lfo->load(lfoXml);
            lfoRate->load(lfoXml);
        }
    }

	EffectParameter(juce::String name, juce::String description, juce::String id, int versionHint, float value, float min, float max, float step = 0.01, bool smoothValueChange = true) : FloatParameter(name, id, versionHint, value, min, max, step), smoothValueChange(smoothValueChange), description(description) {}
};
