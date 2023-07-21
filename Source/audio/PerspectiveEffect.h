#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"
#include "../audio/Effect.h"
#include "../lua/LuaParser.h"

class PerspectiveEffect : public EffectApplication {
public:
	PerspectiveEffect();

	Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) override;
private:
	const juce::String DEFAULT_SCRIPT = "return { x, y, z }";
	juce::MemoryBlock code{DEFAULT_SCRIPT.toRawUTF8(), DEFAULT_SCRIPT.getNumBytesAsUTF8() + 1};
	LuaParser parser{DEFAULT_SCRIPT};

	float currentRotateX = 0;
	float currentRotateY = 0;
	float currentRotateZ = 0;

	float linearSpeedToActualSpeed(float rotateSpeed) {
		return (std::exp(3 * juce::jmin(10.0f, std::abs(rotateSpeed))) - 1) / 50000.0;
	}
};