#include "Effect.h"
#include <numbers>

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, const std::vector<EffectParameter*>& parameters) :
	effectApplication(effectApplication),
	parameters(parameters),
	enabled(nullptr),
	actualValues(std::vector<std::atomic<double>>(parameters.size())) {}

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, EffectParameter* parameter) : Effect(effectApplication, std::vector<EffectParameter*>{parameter}) {}

Effect::Effect(EffectApplicationType application, const std::vector<EffectParameter*>& parameters) :
	application(application),
	parameters(parameters),
	enabled(nullptr),
	actualValues(std::vector<std::atomic<double>>(parameters.size())) {}

Effect::Effect(EffectApplicationType application, EffectParameter* parameter) : Effect(application, std::vector<EffectParameter*>{parameter}) {}

Effect::Effect(const std::vector<EffectParameter*>& parameters) : Effect([](int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {return input;}, parameters) {}

Effect::Effect(EffectParameter* parameter) : Effect([](int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {return input;}, parameter) {}

OsciPoint Effect::apply(int index, OsciPoint input, double volume) {
	animateValues(volume);
	if (application) {
		return application(index, input, actualValues, sampleRate);
	} else if (effectApplication != nullptr) {
		return effectApplication->apply(index, input, actualValues, sampleRate);
	}
	return input;
}

void Effect::animateValues(double volume) {
	for (int i = 0; i < parameters.size(); i++) {
		auto parameter = parameters[i];
		float minValue = parameter->min;
		float maxValue = parameter->max;
		bool lfoEnabled = parameter->lfo != nullptr && parameter->lfo->getValueUnnormalised() != (int)LfoType::Static;
		float phase = lfoEnabled ? nextPhase(parameter) : 0.0;
		float percentage = phase / (2 * std::numbers::pi);
		LfoType type = lfoEnabled ? (LfoType)(int)parameter->lfo->getValueUnnormalised() : LfoType::Static;

		switch (type) {
			case LfoType::Sine:
				actualValues[i] = std::sin(phase) * 0.5 + 0.5;
				actualValues[i] = actualValues[i] * (maxValue - minValue) + minValue;
                break;
			case LfoType::Square:
				actualValues[i] = (percentage < 0.5) ? maxValue : minValue;
				break;
			case LfoType::Seesaw:
				// modified sigmoid function
				actualValues[i] = (percentage < 0.5) ? percentage * 2 : (1 - percentage) * 2;
				actualValues[i] = 1 / (1 + std::exp(-16 * (actualValues[i] - 0.5)));
				actualValues[i] = actualValues[i] * (maxValue - minValue) + minValue;
				break;
			case LfoType::Triangle:
				actualValues[i] = (percentage < 0.5) ? percentage * 2 : (1 - percentage) * 2;
				actualValues[i] = actualValues[i] * (maxValue - minValue) + minValue;
				break;
			case LfoType::Sawtooth:
				actualValues[i] = percentage * (maxValue - minValue) + minValue;
				break;
			case LfoType::ReverseSawtooth:
				actualValues[i] = (1 - percentage) * (maxValue - minValue) + minValue;
				break;
			case LfoType::Noise:
				actualValues[i] = ((float)rand() / RAND_MAX) * (maxValue - minValue) + minValue;
				break;
			default:
				double weight = parameter->smoothValueChange ? 0.0005 : 1.0;
				double newValue;
				if (parameter->sidechain != nullptr && parameter->sidechain->getBoolValue()) {
					newValue = volume * (maxValue - minValue) + minValue;
				} else {
                    newValue = parameter->getValueUnnormalised();
                }
				actualValues[i] = (1.0 - weight) * actualValues[i] + weight * newValue;
				break;
		}
	}
}

// should only be the audio thread calling this, but either way it's not a big deal
float Effect::nextPhase(EffectParameter* parameter) {
	parameter->phase = parameter->phase + parameter->lfoRate->getValueUnnormalised() / sampleRate;

	if (parameter->phase > 1) {
		parameter->phase = parameter->phase - 1;
	}

	return parameter->phase * 2 * std::numbers::pi;
}

void Effect::apply() {
	apply(0, OsciPoint());
}

double Effect::getValue(int index) {
	return parameters[index]->getValueUnnormalised();
}

double Effect::getValue() {
	return getValue(0);
}

double Effect::getActualValue(int index) {
    return actualValues[index];
}

double Effect::getActualValue() {
	return actualValues[0];
}

void Effect::setValue(int index, double value) {
	parameters[index]->setUnnormalisedValueNotifyingHost(value);
}

void Effect::setValue(double value) {
	setValue(0, value);
}

int Effect::getPrecedence() {
	return precedence;
}

void Effect::setPrecedence(int precedence) {
	this->precedence = precedence;
}

void Effect::addListener(int index, juce::AudioProcessorParameter::Listener* listener) {
	parameters[index]->addListener(listener);
	if (parameters[index]->lfo != nullptr) {
		parameters[index]->lfo->addListener(listener);
	}
	if (parameters[index]->lfoRate != nullptr) {
		parameters[index]->lfoRate->addListener(listener);
	}
	if (enabled != nullptr) {
		enabled->addListener(listener);
	}
	if (parameters[index]->sidechain != nullptr) {
        parameters[index]->sidechain->addListener(listener);
    }
}

void Effect::removeListener(int index, juce::AudioProcessorParameter::Listener* listener) {
	if (parameters[index]->sidechain != nullptr) {
        parameters[index]->sidechain->removeListener(listener);
    }
	if (enabled != nullptr) {
		enabled->removeListener(listener);
	}
	if (parameters[index]->lfoRate != nullptr) {
		parameters[index]->lfoRate->removeListener(listener);
	}
	if (parameters[index]->lfo != nullptr) {
		parameters[index]->lfo->removeListener(listener);
	}
    parameters[index]->removeListener(listener);
}

void Effect::markEnableable(bool enable) {
	if (enabled != nullptr) {
        enabled->setValue(enable);
	} else {
		enabled = new BooleanParameter(getName() + " Enabled", getId() + "Enabled", parameters[0]->getVersionHint(), enable, "Toggles the effect.");
	}
}

juce::String Effect::getId() {
	return parameters[0]->paramID;
}

juce::String Effect::getName() {
    return parameters[0]->name;
}

void Effect::save(juce::XmlElement* xml) {
	if (enabled != nullptr) {
		auto enabledXml = xml->createNewChildElement("enabled");
		enabled->save(enabledXml);
	}
	xml->setAttribute("id", getId());
	xml->setAttribute("precedence", precedence);
	for (auto parameter : parameters) {
		parameter->save(xml->createNewChildElement("parameter"));
	}
}

void Effect::load(juce::XmlElement* xml) {
	if (enabled != nullptr) {
		auto enabledXml = xml->getChildByName("enabled");
        if (enabledXml != nullptr) {
            enabled->load(enabledXml);
        }
	}
    if (xml->hasAttribute("precedence")) {
        setPrecedence(xml->getIntAttribute("precedence"));
    }
    for (auto parameterXml : xml->getChildIterator()) {
        auto parameter = getParameter(parameterXml->getStringAttribute("id"));
        if (parameter != nullptr) {
            parameter->load(parameterXml);
        }
    }
}

EffectParameter* Effect::getParameter(juce::String id) {
	for (auto parameter : parameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

void Effect::updateSampleRate(int sampleRate) {
    this->sampleRate = sampleRate;
}
