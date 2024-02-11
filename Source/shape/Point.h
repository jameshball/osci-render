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
	void scale(double x, double y, double z) override;
	void translate(double x, double y, double z) override;
	double length() override;
	double magnitude();
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	void rotate(double rotateX, double rotateY, double rotateZ);
	void normalize();
	double innerProduct(Point& other);

	std::string toString();

	Point& operator=(const Point& other);
	bool operator==(const Point& other);
	bool operator!=(const Point& other);
	Point operator+(const Point& other);
	Point operator-(const Point& other);
	Point operator-();
	Point operator*(const Point& other);
	Point operator*(double scalar);

	double x, y, z;
	
	static double EPSILON;
};