#include "QuadraticBezierCurve.h"

QuadraticBezierCurve::QuadraticBezierCurve(double x1, double y1, double x2, double y2, double x3, double y3) : CubicBezierCurve(x1, y1, x1 + (x2 - x1) * (2.0 / 3.0), y1 + (y2 - y1) * (2.0 / 3.0), x3 + (x2 - x3) * (2.0 / 3.0), y3 + (y2 - y3) * (2.0 / 3.0), x3, y3) {}
