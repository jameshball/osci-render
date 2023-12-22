#pragma once

#include "Shape.h"
#include <cmath>
#include <string>

class Vector2 : public Shape {
public:
	Vector2(double x, double y);
	Vector2(double val);
	Vector2();

	Vector2 nextVector(double drawingProgress) override;
	void rotate(double theta) override;
	void scale(double x, double y) override;
	void translate(double x, double y) override;
	void reflectRelativeToVector(double x, double y);
	double length() override;
	double magnitude();
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	// copy assignment operator
	Vector2& operator=(const Vector2& other);

	double x, y;
	
};