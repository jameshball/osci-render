#pragma once
#include <JuceHeader.h>
#include "../obj/Camera.h"

class PerspectiveEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
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

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<PerspectiveEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Perspective", "Controls the strength of the 3D perspective projection.", "perspectiveStrength", VERSION_HINT, 1.0, 0.0, 1.0),
				new osci::EffectParameter("Focal Length", "Controls the focal length of the 3D perspective effect. A higher focal length makes the image look more flat, and a lower focal length makes the image look more 3D.", "perspectiveFocalLength", VERSION_HINT, 2.0, 0.0, 10.0),
			}
		);
		return eff;
	}

private:

	Camera camera;
};
