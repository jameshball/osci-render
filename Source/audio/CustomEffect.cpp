#include "CustomEffect.h"
#include <numbers>
#include "../MathUtil.h"

const juce::String CustomEffect::FILE_NAME = "6a3580b0-c5fc-4b28-a33e-e26a487f052f";

CustomEffect::CustomEffect(std::function<void(int, juce::String, juce::String)> errorCallback) : errorCallback(errorCallback) {}

CustomEffect::~CustomEffect() {
	parser->close(L);
}

Point CustomEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
	auto effectScale = values[0];

	auto x = input.x;
	auto y = input.y;
	auto z = input.z;

	{
		juce::SpinLock::ScopedLockType lock(codeLock);
		if (!defaultScript) {
			parser->setVariable("x", x);
			parser->setVariable("y", y);
			parser->setVariable("z", z);

			auto result = parser->run(L, LuaVariables{sampleRate, frequency}, step, phase);
			if (result.size() >= 2) {
				x = result[0];
				y = result[1];
				if (result.size() >= 3) {
					z = result[2];
				}
			}
		} else {
			parser->resetErrors();
		}
	}

	step++;
	phase += 2 * std::numbers::pi * frequency / sampleRate;
	phase = MathUtil::wrapAngle(phase);

	return Point(
		(1 - effectScale) * input.x + effectScale * x,
		(1 - effectScale) * input.y + effectScale * y,
		(1 - effectScale) * input.z + effectScale * z
	);
}

void CustomEffect::updateCode(const juce::String& newCode) {
	juce::SpinLock::ScopedLockType lock(codeLock);
	defaultScript = newCode == DEFAULT_SCRIPT;
    code = newCode;
	parser = std::make_unique<LuaParser>(FILE_NAME, code, errorCallback);
}

void CustomEffect::setVariable(juce::String variableName, double value) {
	juce::SpinLock::ScopedLockType lock(codeLock);
	if (!defaultScript) {
        parser->setVariable(variableName, value);
    }
}

juce::String CustomEffect::getCode() {
	juce::SpinLock::ScopedLockType lock(codeLock);
    return code;
}
