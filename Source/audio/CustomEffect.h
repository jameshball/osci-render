#pragma once
#include <JuceHeader.h>
#include "LuaEffectState.h"

class CustomEffect : public osci::EffectApplication {
public:
	CustomEffect(LuaEffectState& luaState, std::atomic<double>* luaValues);
	~CustomEffect();

	// arbitrary UUID
	static const juce::String UNIQUE_ID;
	static const juce::String FILE_NAME;

	std::shared_ptr<osci::EffectApplication> clone() const override {
		return std::make_shared<CustomEffect>(luaState, luaValues);
	}

	osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override;

	std::shared_ptr<osci::Effect> build() const override {
		// Note: This shouldn't be used - CustomEffect requires valid references
		jassertfalse;
		return nullptr;
	}

private:
	LuaEffectState& luaState;
	std::atomic<double>* luaValues;
	
	// Per-voice Lua state
	lua_State *L = nullptr;
	LuaVariables vars;
};
