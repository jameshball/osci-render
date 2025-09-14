#pragma once
#include <JuceHeader.h>
#include "../lua/LuaParser.h"

class CustomEffect : public osci::EffectApplication {
public:
	CustomEffect(std::function<void(int, juce::String, juce::String)> errorCallback, std::atomic<double>* luaValues);
	~CustomEffect();

	// arbitrary UUID
	static const juce::String UNIQUE_ID;
	static const juce::String FILE_NAME;

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override;
	void updateCode(const juce::String& newCode);

	juce::String getCode();
	
	double frequency = 0;

	std::shared_ptr<osci::Effect> build() const override {
		// Note: callers needing CustomEffect with callback/vars should construct directly.
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<CustomEffect>([](int, juce::String, juce::String) {}, nullptr),
			new osci::EffectParameter("Lua Effect", "Controls the strength of the custom Lua effect applied. You can write your own custom effect using Lua by pressing the edit button on the right.", "customEffectStrength", VERSION_HINT, 1.0, 0.0, 1.0));
		eff->setIcon(BinaryData::lua_svg);
		return eff;
	}

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
