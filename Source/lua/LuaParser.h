#pragma once
#include <JuceHeader.h>
#include <regex>
#include <numbers>
#include "../shape/Shape.h"

class ErrorListener {
public:
    virtual void onError(int lineNumber, juce::String error) = 0;
	virtual juce::String getFileName() = 0;
};

struct LuaVariables {
	double sampleRate;
	double frequency;
};

struct lua_State;
class LuaParser {
public:
	LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript = "return { 0.0, 0.0 }");

	std::vector<float> run(lua_State*& L, const LuaVariables vars, long& step, double& phase);
	void setVariable(juce::String variableName, double value);
	bool isFunctionValid();
	juce::String getScript();
	void resetErrors();
	void close(lua_State*& L);

private:
	void reset(lua_State*& L, juce::String script);
	void reportError(const char* error);
	void parse(lua_State*& L);

	int functionRef = -1;
	bool usingFallbackScript = false;
	juce::String script;
	juce::String fallbackScript;
	std::atomic<bool> updateVariables = false;
	juce::SpinLock variableLock;
	std::vector<juce::String> variableNames;
	std::vector<double> variables;
	std::function<void(int, juce::String, juce::String)> errorCallback;
	juce::String fileName;
	std::vector<lua_State*> seenStates;
};