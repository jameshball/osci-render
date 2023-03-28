#include "BulgeEffect.h"

BulgeEffect::BulgeEffect() {}

BulgeEffect::~BulgeEffect() {}

Vector2 BulgeEffect::apply(int index, Vector2 input) {
    double translatedBulge = -value + 1;

    double r = input.magnitude();
    double theta = atan2(input.y, input.x);
    double rn = pow(r, translatedBulge);

    return Vector2(rn * cos(theta), rn * sin(theta));
}

double BulgeEffect::getValue() {
    return value;
}

void BulgeEffect::setValue(double value) {
	this->value = value;
}

void BulgeEffect::setFrequency(double frequency) {
	this->frequency = frequency;
}

int BulgeEffect::getPrecedence() {
    return precedence;
}

void BulgeEffect::setPrecedence(int precedence) {
    this->precedence = precedence;
}

juce::String BulgeEffect::getName() {
    return juce::String("Bulge");
}

juce::String BulgeEffect::getId() {
    return juce::String("bulge");
}
