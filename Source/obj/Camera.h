#pragma once
#include <vector>
#include <memory>
#include "WorldObject.h"
#include "../shape/Shape.h"

class Camera {
public:
	Camera(double focalLength, double x, double y, double z);

	std::vector<std::unique_ptr<Shape>> draw(WorldObject& object);
private:
	double focalLength;
	double x, y, z;
};