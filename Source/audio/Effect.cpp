#include "Effect.h"

Effect::Effect(std::unique_ptr<EffectApplication> effectApplication, juce::String name, juce::String id) : name(name), id(id) {
	this->effectApplication = std::move(effectApplication);
}

Vector2 Effect::apply(int index, Vector2 input) {
	return effectApplication->apply(index, input, value, frequency, sampleRate);
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
