#pragma once
#include "Effect.h"
#include "../shape/Vector2.h"

class BulgeEffect : public Effect {
public:
	BulgeEffect();
	~BulgeEffect();

	Vector2 apply(int index, Vector2 input) override;
	double getValue() override;
	void setValue(double value) override;
	void setFrequency(double frequency) override;
	int getPrecedence() override;
	void setPrecedence(int precedence) override;
	juce::String getName() override;
	juce::String getId() override;

private:
	double value = 0.0;
	double frequency = 1.0;
	int precedence = -1;
};