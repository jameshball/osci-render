#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"
#include "../audio/Effect.h"
#include "../lua/LuaParser.h"
#include "../obj/Frustum.h"

class PerspectiveEffect : public EffectApplication {
public:
	PerspectiveEffect(int versionHint);
	~PerspectiveEffect();

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
	void resetRotation();

	BooleanParameter* fixedRotateX;
	BooleanParameter* fixedRotateY;
	BooleanParameter* fixedRotateZ;

private:
	float currentRotateX = 0;
	float currentRotateY = 0;
	float currentRotateZ = 0;

    double linearSpeedToActualSpeed(double rotateSpeed) {
        double actualSpeed = (std::exp(3 * juce::jmin(10.0, std::abs(rotateSpeed))) - 1) / 50000;
        if (rotateSpeed < 0) {
            actualSpeed *= -1;
        }
        return actualSpeed;
    }
};
