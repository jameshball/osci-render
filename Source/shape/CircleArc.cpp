#include "CircleArc.h"
#include <numbers>
#include "Line.h"

CircleArc::CircleArc(double x, double y, double radiusX, double radiusY, double startAngle, double endAngle) : x(x), y(y), radiusX(radiusX), radiusY(radiusY), startAngle(startAngle), endAngle(endAngle) {}

Vector2 CircleArc::nextVector(double drawingProgress) {
	// scale between start and end angle in the positive direction
	double angle = startAngle + endAngle * drawingProgress;
	return Vector2(
		x + radiusX * std::cos(angle),
		y + radiusY * std::sin(angle)
	);
}

void CircleArc::rotate(double theta) {
	double cosTheta = std::cos(theta);
	double sinTheta = std::sin(theta);

	double newX = x * cosTheta - y * sinTheta;
	double newY = x * sinTheta + y * cosTheta;
	double newWidth = radiusX * cosTheta - radiusY * sinTheta;
	double newHeight = radiusX * sinTheta + radiusY * cosTheta;

	x = newX;
	y = newY;
	radiusX = newWidth;
	radiusY = newHeight;
	
	double newStartAngle = startAngle + theta;
	double newEndAngle = endAngle + theta;
	
	if (newStartAngle > 2 * std::numbers::pi) {
		newStartAngle -= 2 * std::numbers::pi;
	}
	if (newEndAngle > 2 * std::numbers::pi) {
		newEndAngle -= 2 * std::numbers::pi;
	}
	
	startAngle = newStartAngle;
	endAngle = newEndAngle;
}

void CircleArc::scale(double x, double y) {
	this->x *= x;
	this->y *= y;
	this->radiusX *= x;
	this->radiusY *= y;
}

void CircleArc::translate(double x, double y) {
	this->x += x;
	this->y += y;
}

double CircleArc::length() {
	if (len < 0) {
		len = 0;
		double angle = startAngle;
		double step = (endAngle - startAngle) / 500;
		for (int i = 0; i < 500; i++) {
			Vector2 v1 = nextVector(i / 500.0);
			Vector2 v2 = nextVector((i + 1) / 500.0);
			len += Line(v1.x, v1.y, v2.x, v2.y).length();
		}
	}
	return len;
}

std::unique_ptr<Shape> CircleArc::clone() {
	return std::make_unique<CircleArc>(x, y, radiusX, radiusY, startAngle, endAngle);
}

std::string CircleArc::type() {
	return std::string("Arc");
}
