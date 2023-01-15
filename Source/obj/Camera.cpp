#include "Camera.h"
#include "../shape/Line.h"

Camera::Camera(double focalLength, double x, double y, double z) : focalLength(focalLength), x(x), y(y), z(z) {}

std::vector<std::unique_ptr<Shape>> Camera::draw(WorldObject& object)
{
	std::vector<std::unique_ptr<Shape>> shapes;
	for (auto& edge : object.edges) {
        // rotate around x-axis
        double cosValue = std::cos(object.rotateX);
        double sinValue = std::sin(object.rotateX);
        double y2 = cosValue * edge.y1 - sinValue * edge.z1;
        double z2 = sinValue * edge.y1 + cosValue * edge.z1;

        // rotate around y-axis
        cosValue = std::cos(object.rotateY);
        sinValue = std::sin(object.rotateY);
        double x2 = cosValue * edge.x1 + sinValue * z2;
        double z3 = -sinValue * edge.x1 + cosValue * z2;

        // rotate around z-axis
        cosValue = cos(object.rotateZ);
        sinValue = sin(object.rotateZ);
        double x3 = cosValue * x2 - sinValue * y2;
        double y3 = sinValue * x2 + cosValue * y2;

		double startX = x3 * focalLength / (z3 - z) + x;
		double startY = y3 * focalLength / (z3 - z) + y;

        // rotate around x-axis
        cosValue = std::cos(object.rotateX);
        sinValue = std::sin(object.rotateX);
        y2 = cosValue * edge.y2 - sinValue * edge.z2;
        z2 = sinValue * edge.y2 + cosValue * edge.z2;

        // rotate around y-axis
        cosValue = std::cos(object.rotateY);
        sinValue = std::sin(object.rotateY);
        x2 = cosValue * edge.x2 + sinValue * z2;
        z3 = -sinValue * edge.x2 + cosValue * z2;

        // rotate around z-axis
        cosValue = cos(object.rotateZ);
        sinValue = sin(object.rotateZ);
        x3 = cosValue * x2 - sinValue * y2;
        y3 = sinValue * x2 + cosValue * y2;

        double endX = x3 * focalLength / (z3 - z) + x;
        double endY = y3 * focalLength / (z3 - z) + y;

		shapes.push_back(std::make_unique<Line>(startX, startY, endX, endY));
	}
	return shapes;
}
