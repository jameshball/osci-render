#pragma once

#include "Shape.h"
#include "Vector2.h"

class CubicBezierCurve : public Shape {
public:
	CubicBezierCurve(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4);

	Vector2 nextVector(double drawingProgress) override;
	void rotate(double theta) override;
	void scale(double x, double y) override;
	void translate(double x, double y) override;
	double length() override;

	double x1, y1, x2, y2, x3, y3, x4, y4;
	double len = INVALID_LENGTH;
	
};