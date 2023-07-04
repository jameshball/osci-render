#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"

struct lua_State;
class LuaParser {
public:
	LuaParser(juce::String script);
	~LuaParser();

	Vector2 draw();
	bool setVariable(juce::String variableName, double value);

private:
	void parse();

	int functionRef = -1;
	long step = 1;
	lua_State* L;
	juce::String script;
	std::atomic<bool> updateVariables = false;
	std::atomic<bool> accessingVariables = false;
	std::vector<juce::String> variableNames;
	std::vector<double> variables;
};