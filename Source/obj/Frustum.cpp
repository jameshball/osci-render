// FROM https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/lighthouse3d_view_frustum_culling/index.html

#include "Frustum.h"

void Frustum::setCameraInternals(float focalLength, float ratio, float nearDistance, float farDistance) {
	// store the information
	this->focalLength = focalLength;
	this->ratio = ratio;
	this->nearDistance = nearDistance;
	this->farDistance = farDistance;

	// compute width and height of the near section
	float fov = 2 * std::atan(1 / focalLength);
	tang = (float) std::tan(fov * 0.5);
	height = nearDistance * tang;
	width = height * ratio;
}

void Frustum::clipToFrustum(Vec3 &p) {
	float pcz, pcx, pcy, aux;

	// compute and test the Z coordinate
	pcz = p.z;
	pcz = pcz < nearDistance ? nearDistance : (pcz > farDistance ? farDistance : pcz);

	// compute and test the Y coordinate
	pcy = p.y;
	aux = std::abs(pcz * tang);
	pcy = pcy < -aux ? -aux : (pcy > aux ? aux : pcy);

	// compute and test the X coordinate
	pcx = p.x;
	aux = aux * ratio;
	pcx = pcx < -aux ? -aux : (pcx > aux ? aux : pcx);

	p = Vec3(pcx, pcy, pcz);
}