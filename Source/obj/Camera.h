#pragma once
#include <vector>
#include <memory>
#include <JuceHeader.h>
#include "../mathter/Matrix.hpp"
#include "Frustum.h"

using Matrix = mathter::Matrix<float, 4, 4, mathter::eMatrixOrder::PRECEDE_VECTOR, mathter::eMatrixLayout::ROW_MAJOR, false>;

class Camera {
public:
	Camera();

	void setPosition(Vec3& position);
	void findZPos(std::vector<float>& vertices);
	Vec3 toCameraSpace(Vec3& point);
	Vec3 toWorldSpace(Vec3& point);
	void setFocalLength(double focalLength);
	Vec3 project(Vec3& p);
	Frustum getFrustum();
private:
	const double VERTEX_VALUE_THRESHOLD = 1.0;
	const double CAMERA_MOVE_INCREMENT = -0.1;
	const int SAMPLE_RENDER_SAMPLES = 50;
	const int VERTEX_SAMPLES = 1000;
	const int MAX_NUM_STEPS = 1000;

	Frustum frustum;

	Matrix viewMatrix;
};