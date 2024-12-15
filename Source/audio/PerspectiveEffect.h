#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"
#include "../audio/Effect.h"
#include "../obj/Camera.h"

class PerspectiveEffect : public EffectApplication {
public:
	PerspectiveEffect();
	~PerspectiveEffect();

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;

private:

	Camera camera;
};
