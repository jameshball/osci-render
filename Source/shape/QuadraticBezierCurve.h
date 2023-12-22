#pragma once

#include "Shape.h"
#include "CubicBezierCurve.h"

class QuadraticBezierCurve : public CubicBezierCurve {
public:
	QuadraticBezierCurve(double x1, double y1, double x2, double y2, double x3, double y3);
};