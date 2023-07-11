#include "Effect.h"

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectDetails> details) : effectApplication(effectApplication), details(details) {}

Effect::Effect(std::function<Vector2(int, Vector2, std::vector<EffectDetails>, double, int)> application, std::vector<EffectDetails> details) : application(application), details(details) {}

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, juce::String name, juce::String id) {
	this->effectApplication = effectApplication;
    details = std::vector<EffectDetails>(1, EffectDetails{name, id, 0.0});
}

Effect::Effect(juce::String name, juce::String id, double value) {
	details = std::vector<EffectDetails>(1, EffectDetails{name, id, value});
}

Effect::Effect(juce::String name, juce::String id) {
	details = std::vector<EffectDetails>(1, EffectDetails{name, id, 0.0});
}

Effect::Effect(std::function<Vector2(int, Vector2, std::vector<EffectDetails> values, double, int)> application, juce::String name, juce::String id, double value) {
	details = std::vector<EffectDetails>(1, EffectDetails{name, id, value});
	this->application = application;
};

Vector2 Effect::apply(int index, Vector2 input) {
	if (application) {
		return application(index, input, details, frequency, sampleRate);
	} else if (effectApplication != nullptr) {
		return effectApplication->apply(index, input, details, frequency, sampleRate);
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

void Effect::setFrequency(double frequency) {
	this->frequency = frequency;
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
