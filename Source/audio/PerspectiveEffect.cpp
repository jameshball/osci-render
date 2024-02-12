#include "PerspectiveEffect.h"
#include <numbers>
#include "../MathUtil.h"
#include "../obj/Camera.h"

PerspectiveEffect::PerspectiveEffect() {}

PerspectiveEffect::~PerspectiveEffect() {}

Point PerspectiveEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
	auto effectScale = values[0];
	auto focalLength = juce::jmax(values[1], 0.001);

	Vec3 origin = Vec3(0, 0, -focalLength);
	camera.setPosition(origin);
	camera.setFocalLength(focalLength);
	Vec3 vec = Vec3(input.x, input.y, input.z);

	Vec3 projected = camera.project(vec);

	return Point(
		(1 - effectScale) * input.x + effectScale * projected.x,
		(1 - effectScale) * input.y + effectScale * projected.y,
		0
	);
}
