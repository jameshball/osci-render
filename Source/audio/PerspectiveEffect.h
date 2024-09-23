#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"
#include "../audio/Effect.h"
#include "../obj/Camera.h"

class PerspectiveEffect : public EffectApplication {
public:
	PerspectiveEffect();
	~PerspectiveEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate, Point extInput) override;

private:

	Camera camera;
};
