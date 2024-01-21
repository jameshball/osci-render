// FROM https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/lighthouse3d_view_frustum_culling/index.html

#pragma once

#include <JuceHeader.h>
#include "../shape/Point.h"

class Frustum {
public:
	float ratio, nearDistance, farDistance, width, height, tang;

	Point origin = Point(0, 0, 0);

	Point X = Point(1, 0, 0);
	Point Y = Point(0, 1, 0);
	Point Z = Point(0, 0, 1);

	Frustum(float fov, float ratio, float nearDistance, float farDistance) {
        setCameraInternals(fov, ratio, nearDistance, farDistance);
    }
	~Frustum() {};

	void setCameraInternals(float fov, float ratio, float nearD, float farD);
	void clipToFrustum(Point &p);
};
