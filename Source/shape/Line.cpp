#include "Line.h"

Line::Line(double x1, double y1, double x2, double y2) : x1(x1), y1(y1), z1(0), x2(x2), y2(y2), z2(0) {}

Line::Line(double x1, double y1, double z1, double x2, double y2, double z2) : x1(x1), y1(y1), z1(z1), x2(x2), y2(y2), z2(z2) {}

Line::Line(Point p1, Point p2) : x1(p1.x), y1(p1.y), z1(p1.z), x2(p2.x), y2(p2.y), z2(p2.z) {}

Point Line::nextVector(double drawingProgress) {
	return Point(
		x1 + (x2 - x1) * drawingProgress,
		y1 + (y2 - y1) * drawingProgress,
		z1 + (z2 - z1) * drawingProgress
	);
}

void Line::scale(double x, double y, double z) {
	x1 *= x;
	y1 *= y;
	z1 *= z;
	x2 *= x;
	y2 *= y;
	z2 *= z;
}

void Line::translate(double x, double y, double z) {
	x1 += x;
	y1 += y;
	z1 += z;
	x2 += x;
	y2 += y;
	z2 += z;
}

double Line::length(double x1, double y1, double z1, double x2, double y2, double z2) {
	return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) + pow(z2 - z1, 2));
}

double inline Line::length() {
	if (len < 0) {
		len = length(x1, y1, z1, x2, y2, z2);
	}
	return len;
}

std::unique_ptr<Shape> Line::clone() {
	return std::make_unique<Line>(x1, y1, z1, x2, y2, z2);
}

std::string Line::type() {
	return std::string("Line");
}

Line& Line::operator=(const Line& other) {
	if (this == &other) {
		return *this;
	}

	this->x1 = other.x1;
	this->y1 = other.y1;
	this->z1 = other.z1;
	this->x2 = other.x2;
	this->y2 = other.y2;
	this->z2 = other.z2;

	return *this;
}
