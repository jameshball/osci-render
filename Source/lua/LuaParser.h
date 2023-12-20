#pragma once
#include <JuceHeader.h>
#include <regex>
#include "../shape/Shape.h"

class ErrorListener {
public:
    virtual void onError(int lineNumber, juce::String error) = 0;
};

struct lua_State;
class LuaParser {
public:
	LuaParser(juce::String script, std::function<void(int, juce::String)> errorCallback, juce::String fallbackScript = "return { 0.0, 0.0 }");
	~LuaParser();

	std::vector<float> run();
	void setVariable(juce::String variableName, double value);
	bool isFunctionValid();
	juce::String getScript();
	void addErrorListener(ErrorListener* listener);
	void removeErrorListener(ErrorListener* listener);

private:
	void reset(juce::String script);
	void reportError(const char* error);
	void parse();

	static int panic(lua_State* L);

	int functionRef = -1;
	bool usingFallbackScript = false;
	long step = 1;
	lua_State* L = nullptr;
	juce::String script;
	juce::String fallbackScript;
	std::atomic<bool> updateVariables = false;
	juce::SpinLock variableLock;
	std::vector<juce::String> variableNames;
	std::vector<double> variables;
	std::function<void(int, juce::String)> errorCallback;
};