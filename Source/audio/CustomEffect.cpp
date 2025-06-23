#include "CustomEffect.h"
#include <numbers>
#include "../MathUtil.h"

const juce::String CustomEffect::UNIQUE_ID = "6a3580b0-c5fc-4b28-a33e-e26a487f052f";
const juce::String CustomEffect::FILE_NAME = "Custom Lua Effect";

CustomEffect::CustomEffect(std::function<void(int, juce::String, juce::String)> errorCallback, std::atomic<double>* luaValues) : errorCallback(errorCallback), luaValues(luaValues) {
	vars.isEffect = true;
}

CustomEffect::~CustomEffect() {
	parser->close(L);
}

osci::Point CustomEffect::apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
	auto effectScale = values[0].load();

	auto x = input.x;
	auto y = input.y;
	auto z = input.z;

	{
		juce::SpinLock::ScopedLockType lock(codeLock);
		if (!defaultScript) {
			vars.sampleRate = sampleRate;
			vars.frequency = frequency;

			vars.x = x;
			vars.y = y;
			vars.z = z;
			vars.ext_x = extInput.x;
			vars.ext_y = extInput.y;


			std::copy(luaValues, luaValues + 26, std::begin(vars.sliders));

			auto result = parser->run(L, vars);
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

	return osci::Point(
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

juce::String CustomEffect::getCode() {
	juce::SpinLock::ScopedLockType lock(codeLock);
    return code;
}
