#pragma once
#include "../shape/Vector2.h"
#include <optional>

struct SvgState {
	Vector2 currPoint;
	Vector2 initialPoint;
	std::optional<Vector2> prevCubicControlPoint;
	std::optional<Vector2> prevQuadraticControlPoint;
};