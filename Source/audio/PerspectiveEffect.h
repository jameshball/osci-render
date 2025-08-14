#pragma once
#include <JuceHeader.h>
#include "../obj/Camera.h"

class PerspectiveEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		auto effectScale = values[0].load();
		//auto focalLength = juce::jmax(values[1].load(), 0.001);
		double fovDegrees = juce::jmax(values[1].load(), 0.1);
		double fov = juce::degreesToRadians(fovDegrees);
		double focalLength = 1.0 / std::tan(0.5 * fov);


		// Place such that view frustum is tangent to unit sphere
		//Vec3 origin = Vec3(0, 0, -std::sqrt(1 + focalLength * focalLength));
		// TODO
		Vec3 origin = Vec3(0, 0, -1.0f / std::sin(0.5f * (float)fov));
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

private:

	Camera camera;
};
