#pragma once
#include "EffectApplication.h"
#include "../shape/OsciPoint.h"
#include "../audio/Effect.h"
#include "../lua/LuaParser.h"

class CustomEffect : public EffectApplication {
public:
	CustomEffect(std::function<void(int, juce::String, juce::String)> errorCallback, std::atomic<double>* luaValues);
	~CustomEffect();

	// arbitrary UUID
	static const juce::String UNIQUE_ID;
	static const juce::String FILE_NAME;

	OsciPoint apply(int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
	void updateCode(const juce::String& newCode);

	juce::String getCode();
	
	double frequency = 0;

private:
	const juce::String DEFAULT_SCRIPT = "return { x, y, z }";
	juce::String code = DEFAULT_SCRIPT;
	std::function<void(int, juce::String, juce::String)> errorCallback;
	std::unique_ptr<LuaParser> parser = std::make_unique<LuaParser>(FILE_NAME, code, errorCallback);
	juce::SpinLock codeLock;

	bool defaultScript = true;

	lua_State *L = nullptr;

	LuaVariables vars;
	std::atomic<double>* luaValues;
};
