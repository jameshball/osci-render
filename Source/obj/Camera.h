#pragma once
#include <vector>
#include <memory>
#include "WorldObject.h"
#include "../shape/Shape.h"
#include "../shape/Vector2.h"

class Camera {
public:
	Camera(double focalLength, double x, double y, double z);

	std::vector<std::unique_ptr<Shape>> draw(WorldObject& object);
	void findZPos(WorldObject& object);
	void setFocalLength(double focalLength);
private:
	const double VERTEX_VALUE_THRESHOLD = 1.0;
	const double CAMERA_MOVE_INCREMENT = -0.1;
	const int SAMPLE_RENDER_SAMPLES = 50;
	const int VERTEX_SAMPLES = 1000;
	const int MAX_NUM_STEPS = 1000;

	std::atomic<double> focalLength;
	double x, y, z;

	std::vector<Vector2> sampleVerticesInRender(WorldObject& object);
	double maxVertexValue(std::vector<Vector2>& vertices);
	Vector2 project(double x, double y, double z);
};