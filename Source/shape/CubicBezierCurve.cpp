#include "CubicBezierCurve.h"

CubicBezierCurve::CubicBezierCurve(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4) : x1(x1), y1(y1), x2(x2), y2(y2), x3(x3), y3(y3), x4(x4), y4(y4) {}

Vector2 CubicBezierCurve::nextVector(double t) {
	double pow1 = pow(1 - t, 3);
	double pow2 = pow(1 - t, 2);
	double pow3 = pow(t, 2);
	double pow4 = pow(t, 3);

	double x = pow1 * x1 + 3 * pow2 * t * x2 + 3 * (1 - t) * pow3 * x3 + pow4 * x4;
	double y = pow1 * y1 + 3 * pow2 * t * y2 + 3 * (1 - t) * pow3 * y3 + pow4 * y4;
	
	return Vector2(x, y);
}

void CubicBezierCurve::rotate(double theta) {
	double cosTheta = std::cos(theta);
	double sinTheta = std::sin(theta);

	double newX1 = x1 * cosTheta - y1 * sinTheta;
	double newY1 = x1 * sinTheta + y1 * cosTheta;
	double newX2 = x2 * cosTheta - y2 * sinTheta;
	double newY2 = x2 * sinTheta + y2 * cosTheta;
	double newX3 = x3 * cosTheta - y3 * sinTheta;
	double newY3 = x3 * sinTheta + y3 * cosTheta;
	double newX4 = x4 * cosTheta - y4 * sinTheta;
	double newY4 = x4 * sinTheta + y4 * cosTheta;

	x1 = newX1;
	y1 = newY1;
	x2 = newX2;
	y2 = newY2;
	x3 = newX3;
	y3 = newY3;
	x4 = newX4;
	y4 = newY4;
}

void CubicBezierCurve::scale(double x, double y) {
	x1 *= x;
	y1 *= y;
	x2 *= x;
	y2 *= y;
	x3 *= x;
	y3 *= y;
	x4 *= x;
	y4 *= y;
}

void CubicBezierCurve::translate(double x, double y) {
	x1 += x;
	y1 += y;
	x2 += x;
	y2 += y;
	x3 += x;
	y3 += y;
	x4 += x;
	y4 += y;
}

double CubicBezierCurve::length() {
	if (len < 0) {
		// Euclidean distance approximation based on octagonal boundary
		double dx = std::abs(x4 - x1);
		double dy = std::abs(y4 - y1);

		len = 0.41 * std::min(dx, dy) + 0.941246 * std::max(dx, dy);
	}
	return len;
}

std::unique_ptr<Shape> CubicBezierCurve::clone() {
	return std::make_unique<CubicBezierCurve>(x1, y1, x2, y2, x3, y3, x4, y4);
}
