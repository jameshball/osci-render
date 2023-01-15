#pragma once

#include <cmath>

class Vector2 {
public:
	Vector2(double x, double y);
	Vector2(double val);
	Vector2();

	Vector2 nextVector(double drawingProgress);
	void rotate(double theta);
	void scale(double x, double y);
	void translate(double x, double y);
	double length();

	double x, y;
	
};