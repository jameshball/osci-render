#include "Line.h"

Line::Line(double x1, double y1, double x2, double y2) : x1(x1), y1(y1), z1(0), x2(x2), y2(y2), z2(0) {}

Line::Line(double x1, double y1, double z1, double x2, double y2, double z2) : x1(x1), y1(y1), z1(z1), x2(x2), y2(y2), z2(z2) {}

Point Line::nextVector(double drawingProgress) {
	return Point(
		x1 + (x2 - x1) * drawingProgress,
		y1 + (y2 - y1) * drawingProgress,
		z1 + (z2 - z1) * drawingProgress
	);
}

void Line::scale(double x, double y) {
	x1 *= x;
	y1 *= y;
	x2 *= x;
	y2 *= y;
}

void Line::translate(double x, double y) {
	x1 += x;
	y1 += y;
	x2 += x;
	y2 += y;
}

double Line::length(double x1, double y1, double x2, double y2) {
	// Euclidean distance approximation based on octagonal boundary
	double dx = std::abs(x2 - x1);
	double dy = std::abs(y2 - y1);

	return 0.41 * std::min(dx, dy) + 0.941246 * std::max(dx, dy);
}

double inline Line::length() {
	if (len < 0) {
		// Euclidean distance approximation based on octagonal boundary
		double dx = std::abs(x2 - x1);
		double dy = std::abs(y2 - y1);

		len = 0.41 * std::min(dx, dy) + 0.941246 * std::max(dx, dy);
	}
	return len;
}

std::unique_ptr<Shape> Line::clone() {
	return std::make_unique<Line>(x1, y1, x2, y2);
}

std::string Line::type() {
	return std::string("Line");
}
