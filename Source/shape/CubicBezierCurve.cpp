#include "CubicBezierCurve.h"

CubicBezierCurve::CubicBezierCurve(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4) : x1(x1), y1(y1), x2(x2), y2(y2), x3(x3), y3(y3), x4(x4), y4(y4) {}

OsciPoint CubicBezierCurve::nextVector(double t) {
	double pow1 = pow(1 - t, 3);
	double pow2 = pow(1 - t, 2);
	double pow3 = pow(t, 2);
	double pow4 = pow(t, 3);

	double x = pow1 * x1 + 3 * pow2 * t * x2 + 3 * (1 - t) * pow3 * x3 + pow4 * x4;
	double y = pow1 * y1 + 3 * pow2 * t * y2 + 3 * (1 - t) * pow3 * y3 + pow4 * y4;
	
	return OsciPoint(x, y);
}

void CubicBezierCurve::scale(double x, double y, double z) {
	x1 *= x;
	y1 *= y;
	x2 *= x;
	y2 *= y;
	x3 *= x;
	y3 *= y;
	x4 *= x;
	y4 *= y;
}

void CubicBezierCurve::translate(double x, double y, double z) {
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

std::string CubicBezierCurve::type() {
	return std::string("CubicBezierCurve");
}
