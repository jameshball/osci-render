#pragma once
#include <JuceHeader.h>
#include "../modulation/LuaEffectState.h"

class CustomEffect : public osci::EffectApplication {
public:
	CustomEffect(LuaEffectState& luaState, const std::vector<std::shared_ptr<osci::Effect>>& luaSliderEffects);
	~CustomEffect();

	std::shared_ptr<osci::EffectApplication> clone() const override {
		return std::make_shared<CustomEffect>(luaState, luaSliderEffects);
	}

	bool modifiesColour() const override { return lastRunHadColour; }

	osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override;

	std::shared_ptr<osci::Effect> build() const override {
		// Note: This shouldn't be used - CustomEffect requires valid references
		jassertfalse;
		return nullptr;
	}

private:
	LuaEffectState& luaState;
	const std::vector<std::shared_ptr<osci::Effect>>& luaSliderEffects;
	
	// Per-voice Lua state
	lua_State *L = nullptr;
	LuaVariables vars;
	mutable bool lastRunHadColour = false;
};
