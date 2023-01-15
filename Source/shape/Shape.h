#pragma once

#include <cmath>
#include <cstdlib>
#include <vector>
#include <memory>
#include "Vector2.h"

class Shape {
public:
	virtual Vector2 nextVector(double drawingProgress) = 0;
	virtual void rotate(double theta) = 0;
	virtual void scale(double x, double y) = 0;
	virtual void translate(double x, double y) = 0;
	virtual double length() = 0;

	static double totalLength(std::vector<std::unique_ptr<Shape>>&);

	const double INVALID_LENGTH = -1.0;
};