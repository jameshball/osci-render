#pragma once
#include <JuceHeader.h>
#include "../shape/Shape.h"

struct lua_State;
class LuaParser {
public:
	LuaParser(juce::String script, juce::String LuaParser = "return { 0.0, 0.0 }");
	~LuaParser();

	std::vector<float> run();
	void setVariable(juce::String variableName, double value);
	bool isFunctionValid();
	juce::String getScript();

private:
	void reset(juce::String script);
	void parse();

	static int panic(lua_State* L);

	int functionRef = -1;
	long step = 1;
	lua_State* L = nullptr;
	juce::String script;
	juce::String fallbackScript;
	std::atomic<bool> updateVariables = false;
	juce::SpinLock variableLock;
	std::vector<juce::String> variableNames;
	std::vector<double> variables;
};