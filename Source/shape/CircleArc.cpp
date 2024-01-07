#include "CircleArc.h"
#include <numbers>
#include "Line.h"

CircleArc::CircleArc(double x, double y, double radiusX, double radiusY, double startAngle, double endAngle) : x(x), y(y), radiusX(radiusX), radiusY(radiusY), startAngle(startAngle), endAngle(endAngle) {}

Point CircleArc::nextVector(double drawingProgress) {
	// scale between start and end angle in the positive direction
	double angle = startAngle + endAngle * drawingProgress;
	return Point(
		x + radiusX * std::cos(angle),
		y + radiusY * std::sin(angle)
	);
}

void CircleArc::scale(double x, double y, double z) {
	this->x *= x;
	this->y *= y;
	this->radiusX *= x;
	this->radiusY *= y;
}

void CircleArc::translate(double x, double y, double z) {
	this->x += x;
	this->y += y;
}

double CircleArc::length() {
	if (len < 0) {
		len = 0;
        // TODO: Replace this, it's stupid. Do a real approximation.
        int segments = 5;
		Point start;
		Point end = nextVector(0);
		for (int i = 0; i < segments; i++) {
			start = end;
			end = nextVector((i + 1) / (double) segments);
			len += Line::length(start.x, start.y, start.z, end.x, end.y, end.z);
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
