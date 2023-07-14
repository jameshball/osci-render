#include "Effect.h"

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectDetails> details, bool smoothValueChange) : effectApplication(effectApplication), details(details), smoothValueChange(smoothValueChange) {
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, std::vector<EffectDetails> details, bool smoothValueChange) : application(application), details(details), smoothValueChange(smoothValueChange) {
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, juce::String name, juce::String id, bool smoothValueChange) : smoothValueChange(smoothValueChange) {
	this->effectApplication = effectApplication;
    details = std::vector<EffectDetails>(1, EffectDetails{name, id, 0.0});
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(juce::String name, juce::String id, double value, bool smoothValueChange) : smoothValueChange(smoothValueChange) {
	details = std::vector<EffectDetails>(1, EffectDetails{name, id, value});
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(juce::String name, juce::String id, bool smoothValueChange) : smoothValueChange(smoothValueChange) {
	details = std::vector<EffectDetails>(1, EffectDetails{name, id, 0.0});
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>& values, double)> application, juce::String name, juce::String id, double value, bool smoothValueChange) : smoothValueChange(smoothValueChange) {
	details = std::vector<EffectDetails>(1, EffectDetails{name, id, value});
	smoothValues = std::vector<double>(details.size(), 0.0);
	this->application = application;
};

Vector2 Effect::apply(int index, Vector2 input) {
	double weight = smoothValueChange ? 0.0005 : 1.0;
	for (int i = 0; i < details.size(); i++) {
        smoothValues[i] = (1.0 - weight) * smoothValues[i] + weight * details[i].value;
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
	return details[index].value;
}

double Effect::getValue() {
	return getValue(0);
}

std::vector<EffectDetails> Effect::getDetails() {
	return details;
}

void Effect::setValue(int index, double value) {
	details[index].value = value;
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

juce::String Effect::getId() {
	return details[0].id;
}

juce::String Effect::getName() {
    return details[0].name;
}
