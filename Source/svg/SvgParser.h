#pragma once
#include "../shape/Vector2.h"

struct {
	Vector2 currPoint;
	Vector2 initialPoint;
	Vector2 prevCubicControlPoint;
	Vector2 prevQuadraticControlPoint;
};