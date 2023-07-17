#include "Effect.h"

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, std::vector<EffectDetails> details) : effectApplication(effectApplication), details(details) {
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(std::shared_ptr<EffectApplication> effectApplication, EffectDetails details) : Effect(effectApplication, std::vector<EffectDetails>{details}) {}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, std::vector<EffectDetails> details) : application(application), details(details) {
	smoothValues = std::vector<double>(details.size(), 0.0);
}

Effect::Effect(std::function<Vector2(int, Vector2, const std::vector<double>&, double)> application, EffectDetails details) : Effect(application, std::vector<EffectDetails>{details}) {}

Vector2 Effect::apply(int index, Vector2 input) {
	for (int i = 0; i < details.size(); i++) {
		double weight = details[i].smoothValueChange ? 0.0005 : 1.0;
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
