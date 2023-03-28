#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>

class EffectApplication {
public:
	EffectApplication() {};

	virtual Vector2 apply(int index, Vector2 input, double value) = 0;
};