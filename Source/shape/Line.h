#pragma once

#include "Shape.h"
#include "Point.h"

class Line : public Shape {
public:
	Line(double x1, double y1, double x2, double y2);
	Line(double x1, double y1, double z1, double x2, double y2, double z2);

	Point nextVector(double drawingProgress) override;
	void scale(double x, double y) override;
	void translate(double x, double y) override;
	static double length(double x1, double y1, double x2, double y2);
	double length() override;
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	double x1, y1, z1, x2, y2, z2;
	
};