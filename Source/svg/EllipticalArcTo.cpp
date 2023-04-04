#include "EllipticalArcTo.h"
#include "../shape/Line.h"
#include <numbers>
#include "../shape/CircleArc.h"

std::vector<std::unique_ptr<Shape>> EllipticalArcTo::absolute(SvgState& state, std::vector<float>& args) {
	return parseEllipticalArc(state, args, true);
}

std::vector<std::unique_ptr<Shape>> EllipticalArcTo::relative(SvgState& state, std::vector<float>& args) {
	return parseEllipticalArc(state, args, false);
}

std::vector<std::unique_ptr<Shape>> EllipticalArcTo::parseEllipticalArc(SvgState& state, std::vector<float>& args, bool isAbsolute) {
	if (args.size() % 7 != 0 || args.size() < 7) {
		return {};
	}
	
	std::vector<std::unique_ptr<Shape>> shapes;
	for (int i = 0; i < args.size(); i += 7) {
		Vector2 newPoint(args[i + 5], args[i + 6]);
		if (!isAbsolute) {
            newPoint.translate(state.currPoint.x, state.currPoint.y);
		}
		
		createArc(shapes, state.currPoint, args[i], args[i + 1], args[i + 2], args[i + 3] == 1, args[i + 4] == 1, newPoint);
		state.currPoint = newPoint;
	}

	return shapes;
}

// The following algorithm is completely based on https://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
void EllipticalArcTo::createArc(std::vector<std::unique_ptr<Shape>>& shapes, Vector2 start, float rx, float ry, float theta, bool largeArcFlag, bool sweepFlag, Vector2 end) {
    double x2 = end.x;
    double y2 = end.y;
    // Ensure radii are valid
    if (rx == 0 || ry == 0) {
        shapes.push_back(std::make_unique<Line>(start.x, start.y, end.x, end.y));
        return;
    }
    double x1 = start.x;
    double y1 = start.y;
    // Compute the half distance between the current and the final point
    double dx2 = (x1 - x2) / 2.0;
    double dy2 = (y1 - y2) / 2.0;
    // Convert theta from degrees to radians
    
    theta = std::fmod(theta, 360.0) * (std::numbers::pi / 180.0);

    //
    // Step 1 : Compute (x1', y1')
    //
    double x1prime = std::cos(theta) * dx2 + std::sin(theta) * dy2;
    double y1prime = -std::sin(theta) * dx2 + std::cos(theta) * dy2;
    // Ensure radii are large enough
    rx = std::abs(rx);
    ry = std::abs(ry);
    double Prx = rx * rx;
    double Pry = ry * ry;
    double Px1prime = x1prime * x1prime;
    double Py1prime = y1prime * y1prime;
    double d = Px1prime / Prx + Py1prime / Pry;
    if (d > 1) {
        rx = std::abs(std::sqrt(d) * rx);
        ry = std::abs(std::sqrt(d) * ry);
        Prx = rx * rx;
        Pry = ry * ry;
    }

    //
    // Step 2 : Compute (cx', cy')
    //
    double sign = (largeArcFlag == sweepFlag) ? -1.0 : 1.0;
    // Forcing the inner term to be positive. It should be >= 0 but can sometimes be negative due
    // to double precision.
    double coef = sign *
        std::sqrt(std::abs((Prx * Pry) - (Prx * Py1prime) - (Pry * Px1prime)) / ((Prx * Py1prime) + (Pry * Px1prime)));
    double cxprime = coef * ((rx * y1prime) / ry);
    double cyprime = coef * -((ry * x1prime) / rx);

    //
    // Step 3 : Compute (cx, cy) from (cx', cy')
    //
    double sx2 = (x1 + x2) / 2.0;
    double sy2 = (y1 + y2) / 2.0;
    double cx = sx2 + std::cos(theta) * cxprime - std::sin(theta) * cyprime;
    double cy = sy2 + std::sin(theta) * cxprime + std::cos(theta) * cyprime;

    //
    // Step 4 : Compute the angleStart (theta1) and the angleExtent (dtheta)
    //
    double ux = (x1prime - cxprime) / rx;
    double uy = (y1prime - cyprime) / ry;
    double vx = (-x1prime - cxprime) / rx;
    double vy = (-y1prime - cyprime) / ry;
    double p, n;
    // Compute the angle start
    n = std::sqrt((ux * ux) + (uy * uy));
    p = ux; // (1 * ux) + (0 * uy)
    sign = (uy < 0) ? -1.0 : 1.0;
    double angleStart = sign * std::acos(p / n);
    // Compute the angle extent
    n = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    p = ux * vx + uy * vy;
    sign = (ux * vy - uy * vx < 0) ? -1.0 : 1.0;
    double angleExtent = sign * std::acos(p / n);
    if (!sweepFlag && angleExtent > 0) {
		angleExtent -= 2 * std::numbers::pi;
    } else if (sweepFlag && angleExtent < 0) {
        angleExtent += 2 * std::numbers::pi;
    }
	angleExtent = std::fmod(angleExtent, 2 * std::numbers::pi);
	angleStart = std::fmod(angleStart, 2 * std::numbers::pi);

	auto arc = std::make_unique<CircleArc>(cx, cy, rx, ry, angleStart, angleExtent);
	arc->translate(-cx, -cy);
	arc->rotate(theta);
	arc->translate(cx, cy);

	Vector2 startPoint = arc->nextVector(0);
	Vector2 endPoint = arc->nextVector(1);

    shapes.push_back(std::move(arc));
}
