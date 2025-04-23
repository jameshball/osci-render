#include "PerspectiveEffect.h"
#include <numbers>
#include "../MathUtil.h"
#include "../obj/Camera.h"

PerspectiveEffect::PerspectiveEffect() {}

PerspectiveEffect::~PerspectiveEffect() {}

osci::Point PerspectiveEffect::apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
	auto effectScale = values[0].load();
	auto focalLength = juce::jmax(values[1].load(), 0.001);

	Vec3 origin = Vec3(0, 0, -focalLength);
	camera.setPosition(origin);
	camera.setFocalLength(focalLength);
	Vec3 vec = Vec3(input.x, input.y, input.z);

	Vec3 projected = camera.project(vec);

	return osci::Point(
		(1 - effectScale) * input.x + effectScale * projected.x,
		(1 - effectScale) * input.y + effectScale * projected.y,
		0
	);
}
