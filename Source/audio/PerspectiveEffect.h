#pragma once
#include <JuceHeader.h>
#include "../obj/Camera.h"

class PerspectiveEffect : public osci::EffectApplication {
public:
	PerspectiveEffect();
	~PerspectiveEffect();

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:

	Camera camera;
};
