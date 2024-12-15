#pragma once

#include "Shape.h"
#include <cmath>
#include <string>

class OsciPoint : public Shape {
public:
	OsciPoint(double x, double y, double z);
	OsciPoint(double x, double y);
	OsciPoint(double val);
	OsciPoint();

	OsciPoint nextVector(double drawingProgress) override;
	void scale(double x, double y, double z) override;
	void translate(double x, double y, double z) override;
	double length() override;
	double magnitude();
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	void rotate(double rotateX, double rotateY, double rotateZ);
	void normalize();
	double innerProduct(OsciPoint& other);

	std::string toString();

	OsciPoint& operator=(const OsciPoint& other);
	bool operator==(const OsciPoint& other);
	bool operator!=(const OsciPoint& other);
	OsciPoint operator+(const OsciPoint& other);
	OsciPoint operator+(double scalar);
	friend OsciPoint operator+(double scalar, const OsciPoint& point);
	OsciPoint operator-(const OsciPoint& other);
	OsciPoint operator-();
	OsciPoint operator*(const OsciPoint& other);
	OsciPoint operator*(double scalar);
	friend OsciPoint operator*(double scalar, const OsciPoint& point);

	double x, y, z;
	
	static double EPSILON;
};
