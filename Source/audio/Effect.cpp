#include "Effect.h"
#include <numbers>

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectParameter*> parameters) : effectApplication(effectApplication), parameters(parameters), enabled(nullptr) {
	actualValues = std::vector<double>(parameters.size(), 0.0);
}

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, EffectParameter* parameter) : Effect(effectApplication, std::vector<EffectParameter*>{parameter}) {}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, std::vector<EffectParameter*> parameters) : application(application), parameters(parameters), enabled(nullptr) {
	actualValues = std::vector<double>(parameters.size(), 0.0);
}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, EffectParameter* parameter) : Effect(application, std::vector<EffectParameter*>{parameter}) {}

Vector2 Effect::apply(int index, Vector2 input) {
	animateValues();
	if (application) {
		return application(index, input, actualValues, sampleRate);
	} else if (effectApplication != nullptr) {
		return effectApplication->apply(index, input, actualValues, sampleRate);
	}
	return input;
}

void Effect::animateValues() {
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
				actualValues[i] = (1.0 - weight) * actualValues[i] + weight * parameter->getValueUnnormalised();
				break;
		}
	}
}

// should only be the audio thread calling this, but either way it's not a big deal
float Effect::nextPhase(EffectParameter* parameter) {
	parameter->phase += parameter->lfoRate->getValueUnnormalised() / sampleRate;

	if (parameter->phase > 1) {
		parameter->phase -= 1;
	}

	return parameter->phase * 2 * std::numbers::pi;
}

void Effect::apply() {
	apply(0, Vector2());
}

double Effect::getValue(int index) {
	return parameters[index]->getValueUnnormalised();
}

double Effect::getValue() {
	return getValue(0);
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
}

void Effect::removeListener(int index, juce::AudioProcessorParameter::Listener* listener) {
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
		enabled = new BooleanParameter(getName() + " Enabled", getId() + "Enabled", enable);
	}
}

juce::String Effect::getId() {
	return parameters[0]->paramID;
}

juce::String Effect::getName() {
    return parameters[0]->name;
}
