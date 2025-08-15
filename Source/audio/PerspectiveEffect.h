#pragma once
#include <JuceHeader.h>
#include "../obj/Camera.h"

class PerspectiveEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		auto effectScale = values[0].load();
		// Far plane clipping happens at about 1.2 deg for 100 far plane dist
		double fovDegrees = juce::jlimit(1.5, 179.0, values[1].load());
		double fov = juce::degreesToRadians(fovDegrees);

		// Place camera such that field of view is tangent to unit sphere
		Vec3 origin = Vec3(0, 0, -1.0f / std::sin(0.5f * (float)fov));
		camera.setPosition(origin);
		camera.setFov(fov);
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
