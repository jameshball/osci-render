#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"
#include "../audio/Effect.h"
#include "../obj/Camera.h"

class PerspectiveEffect : public EffectApplication {
public:
	PerspectiveEffect();
	~PerspectiveEffect();

	Point apply(int index, Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:

	Camera camera;
};
