#pragma once

#include "Shape.h"
#include "OsciPoint.h"

class CubicBezierCurve : public Shape {
public:
	CubicBezierCurve(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4);

	OsciPoint nextVector(double drawingProgress) override;
	void scale(double x, double y, double z) override;
	void translate(double x, double y, double z) override;
	double length() override;
	std::unique_ptr<Shape> clone() override;
	std::string type() override;

	double x1, y1, x2, y2, x3, y3, x4, y4;
	
};
