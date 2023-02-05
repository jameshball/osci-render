#pragma once

#include "Shape.h"
#include "Vector2.h"

class Line : public Shape {
public:
	Line(double x1, double y1, double x2, double y2);

	Vector2 nextVector(double drawingProgress) override;
	void rotate(double theta) override;
	void scale(double x, double y) override;
	void translate(double x, double y) override;
	double length() override;
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	double x1, y1, x2, y2;
	
};