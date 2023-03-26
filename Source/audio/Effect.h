#pragma once
#include "../shape/Vector2.h"

class Effect {
public:
	Effect();

	virtual Vector2 apply(int index, Vector2 input) = 0;
	virtual void setValue(double value) = 0;
	virtual void setFrequency(double frequency) = 0;
	virtual int getPrecedence() = 0;
	virtual void setPrecedence(int precedence) = 0;
};