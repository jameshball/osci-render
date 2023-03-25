#pragma once
#include "../shape/Vector2.h"

class Effect {
public:
	Effect();

	virtual Vector2 apply(int index, Vector2 input) = 0;
};