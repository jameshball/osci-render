#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"
#include "../audio/Effect.h"
#include "../lua/LuaParser.h"

class PerspectiveEffect : public EffectApplication {
public:
	PerspectiveEffect(int versionHint);

	Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) override;
	void updateCode(const juce::String& newCode);
	juce::String getCode();

	BooleanParameter* fixedRotateX;
	BooleanParameter* fixedRotateY;
	BooleanParameter* fixedRotateZ;
private:
	const juce::String DEFAULT_SCRIPT = "return { x, y, z }";
	juce::String code = DEFAULT_SCRIPT;
	juce::SpinLock codeLock;
	std::unique_ptr<LuaParser> parser = std::make_unique<LuaParser>(code);
	bool defaultScript = true;

	float currentRotateX = 0;
	float currentRotateY = 0;
	float currentRotateZ = 0;
    
    int versionHint;

	float linearSpeedToActualSpeed(float rotateSpeed) {
		return (std::exp(3 * juce::jmin(10.0f, std::abs(rotateSpeed))) - 1) / 50000.0;
	}
};
