#include "Effect.h"

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectParameter> parameters) : effectApplication(effectApplication), parameters(parameters), enabled(parameters[0].name + " Enabled", parameters[0].id + "Enabled", true) {
	smoothValues = std::vector<double>(parameters.size(), 0.0);
}

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, EffectParameter parameter) : Effect(effectApplication, std::vector<EffectParameter>{parameter}) {}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, std::vector<EffectParameter> parameters) : application(application), parameters(parameters), enabled(parameters[0].name + " Enabled", parameters[0].id + "Enabled", true) {
	smoothValues = std::vector<double>(parameters.size(), 0.0);
}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, EffectParameter parameter) : Effect(application, std::vector<EffectParameter>{parameter}) {}

Vector2 Effect::apply(int index, Vector2 input) {
	for (int i = 0; i < parameters.size(); i++) {
		double weight = parameters[i].smoothValueChange ? 0.0005 : 1.0;
        smoothValues[i] = (1.0 - weight) * smoothValues[i] + weight * parameters[i].getValueUnnormalised();
    }
	if (application) {
		return application(index, input, smoothValues, sampleRate);
	} else if (effectApplication != nullptr) {
		return effectApplication->apply(index, input, smoothValues, sampleRate);
	}
	return input;
}

void Effect::apply() {
	apply(0, Vector2());
}

double Effect::getValue(int index) {
	return parameters[index].getValueUnnormalised();
}

double Effect::getValue() {
	return getValue(0);
}

void Effect::setValue(int index, double value) {
	parameters[index].setUnnormalisedValueNotifyingHost(value);
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
	juce::SpinLock::ScopedLockType lock(listenerLock);
	parameters[index].addListener(listener);
	enabled.addListener(listener);
}

void Effect::removeListener(int index, juce::AudioProcessorParameter::Listener* listener) {
    juce::SpinLock::ScopedLockType lock(listenerLock);
    parameters[index].removeListener(listener);
    enabled.removeListener(listener);
}

juce::String Effect::getId() {
	return parameters[0].id;
}

juce::String Effect::getName() {
    return parameters[0].name;
}
