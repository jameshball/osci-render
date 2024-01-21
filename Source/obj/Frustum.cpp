// FROM https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/lighthouse3d_view_frustum_culling/index.html

#include "Frustum.h"

void Frustum::setCameraInternals(float focalLength, float ratio, float nearDistance, float farDistance) {
	// store the information
	this->ratio = ratio;
	this->nearDistance = nearDistance;
	this->farDistance = farDistance;

	// compute width and height of the near section
	float fov = 2 * std::atan(1 / (focalLength * 2));
	origin = Point(0, 0, -focalLength);
	tang = (float) std::tan(fov * 0.5);
	height = nearDistance * tang;
	width = height * ratio;
}

void Frustum::clipToFrustum(Point &p) {
	float pcz, pcx, pcy, aux;

	// compute vector from camera position to p
	Point v = p - origin;

	// compute and test the Z coordinate
	Point negZ = -Z;
	pcz = v.innerProduct(Z);
	pcz = juce::jlimit(nearDistance, farDistance, pcz);

	// compute and test the Y coordinate
	pcy = v.innerProduct(Y);
	aux = pcz * tang;
	pcy = juce::jlimit(-aux, aux, pcy);

	// compute and test the X coordinate
	pcx = v.innerProduct(X);
	aux = aux * ratio;
	pcx = juce::jlimit(-aux, aux, pcx);

	// calculate the clipped point using the referential coordinates
	Point x = X * pcx;
	Point y = Y * pcy;
	Point z = Z * pcz;

	p = x + y + z;
}