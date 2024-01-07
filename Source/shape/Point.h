#pragma once

#include "Shape.h"
#include <cmath>
#include <string>

class Point : public Shape {
public:
	Point(double x, double y, double z);
	Point(double x, double y);
	Point(double val);
	Point();

	Point nextVector(double drawingProgress) override;
	void scale(double x, double y) override;
	void translate(double x, double y) override;
	void reflectRelativeToVector(double x, double y);
	double length() override;
	double magnitude();
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	void rotate(double rotateX, double rotateY, double rotateZ);

	// copy assignment operator
	Point& operator=(const Point& other);

	double x, y, z;
	
};