#include "CustomEffect.h"
#include <numbers>
#include "../MathUtil.h"

CustomEffect::CustomEffect(LuaEffectState& luaState, std::atomic<double>* luaValues) 
	: luaState(luaState), luaValues(luaValues) {
	vars.isEffect = true;
}

CustomEffect::~CustomEffect() {
	if (luaState.parser) {
		luaState.parser->close(L);
	}
}

osci::Point CustomEffect::apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
	if (!luaState.parser || !luaValues) {
		return input;
	}
	
	auto effectScale = values[0].load();

	auto x = input.x;
	auto y = input.y;
	auto z = input.z;

	{
		juce::SpinLock::ScopedLockType lock(luaState.codeLock);
		if (!luaState.defaultScript) {
			vars.sampleRate = sampleRate;
			vars.frequency = frequency;

			vars.x = x;
			vars.y = y;
			vars.z = z;
			vars.ext_x = externalInput.x;
			vars.ext_y = externalInput.y;

			std::copy(luaValues, luaValues + 26, std::begin(vars.sliders));

			auto result = luaState.parser->run(L, vars);
			if (result.size() >= 2) {
				x = result[0];
				y = result[1];
				if (result.size() >= 3) {
					z = result[2];
				}
			}
		} else {
			luaState.parser->resetErrors();
		}
	}

	return osci::Point(
		(1 - effectScale) * input.x + effectScale * x,
		(1 - effectScale) * input.y + effectScale * y,
		(1 - effectScale) * input.z + effectScale * z
	);
}
