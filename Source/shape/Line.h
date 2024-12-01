#pragma once

#include "Shape.h"
#include "OsciPoint.h"

class Line : public Shape {
public:
	Line(double x1, double y1, double x2, double y2);
	Line(double x1, double y1, double z1, double x2, double y2, double z2);
	Line(OsciPoint p1, OsciPoint p2);

	OsciPoint nextVector(double drawingProgress) override;
	void scale(double x, double y, double z) override;
	void translate(double x, double y, double z) override;
	static double length(double x1, double y1, double z1, double x2, double y2, double z2);
	double length() override;
	std::unique_ptr<Shape> clone() override;
	std::string type() override;
	Line& operator=(const Line& other);

	double x1, y1, z1, x2, y2, z2;
	
};
