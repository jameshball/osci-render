#include "Effect.h"

Effect::Effect(std::unique_ptr<EffectApplication> effectApplication, juce::String name, juce::String id) : name(name), id(id) {
	this->effectApplication = std::move(effectApplication);
}

Effect::Effect(juce::String name, juce::String id) : name(name), id(id) {}

Effect::Effect(juce::String name, juce::String id, double value) : name(name), id(id), value(value) {}

Effect::Effect(std::function<Vector2(int, Vector2, double, double, int)> application, juce::String name, juce::String id, double value) : Effect(name, id, value) {
	this->application = application;
};

Vector2 Effect::apply(int index, Vector2 input) {
	if (application) {
		return application(index, input, value, frequency, sampleRate);
	} else if (effectApplication != nullptr) {
		return effectApplication->apply(index, input, value, frequency, sampleRate);
	}
	return input;
}

void Effect::apply() {
	apply(0, Vector2());
}

double Effect::getValue() {
	return value;
}

void Effect::setValue(double value) {
	this->value = value;
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

juce::String Effect::getName() {
	return name;
}

juce::String Effect::getId() {
	return id;
}
