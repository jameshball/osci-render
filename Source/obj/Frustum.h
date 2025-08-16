// FROM https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/lighthouse3d_view_frustum_culling/index.html

#pragma once

#include "../mathter/Matrix.hpp"
#include <cmath>

using Vec3 = mathter::Vector<float, 3, false>;

class Frustum {
public:
	float ratio, nearDistance, farDistance, width, height, tang, fov, focalLength;

	Frustum(float fov, float ratio, float nearDistance, float farDistance) {
        setCameraInternals(fov, ratio, nearDistance, farDistance);
    }
	~Frustum() {};

	void setCameraInternals(float fov, float ratio, float nearD, float farD);
	void clipToFrustum(Vec3 &p);
};
