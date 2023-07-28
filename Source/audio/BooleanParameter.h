#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

class BooleanParameter : public juce::AudioProcessorParameterWithID {
public:
	BooleanParameter(juce::String name, juce::String id, bool value) : AudioProcessorParameterWithID(id, name), value(value) {}

	juce::String getName(int maximumStringLength) const override {
		return name.substring(0, maximumStringLength);
	}

	juce::String getLabel() const override {
        return juce::String();
    }

	float getValue() const override {
		return value.load();
	}

	bool getBoolValue() const {
        return value.load();
    }

	void setValue(float newValue) override {
		value.store(newValue >= 0.5f);
    }

	void setBoolValue(bool newValue) {
		value.store(newValue);
	}

	void setBoolValueNotifyingHost(bool newValue) {
        setValueNotifyingHost(newValue ? 1.0f : 0.0f);
    }

	float getDefaultValue() const override {
        return false;
    }

	int getNumSteps() const override {
		return 2;
    }

	bool isDiscrete() const override {
        return true;
    }

	bool isBoolean() const override {
        return true;
    }

	bool isOrientationInverted() const override {
        return false;
    }

	juce::String getText(float value, int maximumStringLength) const override {
		juce::String string = value ? "true" : "false";
		return string.substring(0, maximumStringLength);
	}

	float getValueForText(const juce::String& text) const override {
		return text.length() > 0 && text.toLowerCase()[0] == 't';
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
    }

	void load(juce::XmlElement* xml) {
		setBoolValueNotifyingHost(xml->getBoolAttribute("value", getDefaultValue()));
    }

private:
	std::atomic<bool> value = false;
};