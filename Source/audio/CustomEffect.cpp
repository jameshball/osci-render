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
	auto r = input.r;
	auto g = input.g;
	auto b = input.b;
	bool resultHasColour = (input.r >= 0.0f);

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
			if (result.count >= 6) {
				x = result.values[0];
				y = result.values[1];
				z = result.values[2];
				r = result.values[3];
				g = result.values[4];
				b = result.values[5];
				resultHasColour = true;
			} else if (result.count >= 3) {
				x = result.values[0];
				y = result.values[1];
				z = result.values[2];
			} else if (result.count >= 2) {
				x = result.values[0];
				y = result.values[1];
			}
		} else {
			luaState.parser->resetErrors();
		}
	}

	osci::Point blended(
		(1 - effectScale) * input.x + effectScale * x,
		(1 - effectScale) * input.y + effectScale * y,
		(1 - effectScale) * input.z + effectScale * z
	);

	lastRunHadColour = resultHasColour;

	if (resultHasColour) {
		float inR = input.r >= 0.0f ? input.r : 0.0f;
		float inG = input.r >= 0.0f ? input.g : 0.0f;
		float inB = input.r >= 0.0f ? input.b : 0.0f;
		blended = blended.withColour(
			(1 - effectScale) * inR + effectScale * r,
			(1 - effectScale) * inG + effectScale * g,
			(1 - effectScale) * inB + effectScale * b
		);
	}

	return blended;
}
