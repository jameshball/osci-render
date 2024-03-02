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

	const int NUM_SLIDERS = 26;

	const char SLIDER_NAMES[NUM_SLIDERS][9] = {
		"slider_a",
		"slider_b",
		"slider_c",
		"slider_d",
		"slider_e",
		"slider_f",
		"slider_g",
		"slider_h",
		"slider_i",
		"slider_j",
		"slider_k",
		"slider_l",
		"slider_m",
		"slider_n",
		"slider_o",
		"slider_p",
		"slider_q",
		"slider_r",
		"slider_s",
		"slider_t",
		"slider_u",
		"slider_v",
		"slider_w",
		"slider_x",
		"slider_y",
		"slider_z",
	};

	struct LuaVariables {

		double sliders[NUM_SLIDERS] = { 0.0 };

		double step = 1;
		double phase = 0;
		double sampleRate = 0;
		double frequency = 0;

		// x, y, z are only used for effects
		bool isEffect = false;

		double x = 0;
		double y = 0;
		double z = 0;
	};

	struct lua_State;
	class LuaParser {
	public:
		LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript = "return { 0.0, 0.0 }");

		std::vector<float> run(lua_State*& L, LuaVariables& vars);
		bool isFunctionValid();
		juce::String getScript();
		void resetErrors();
		void close(lua_State*& L);

		static std::function<void(const std::string&)> onPrint;

	private:
		void reset(lua_State*& L, juce::String script);
		void reportError(const char* error);
		void parse(lua_State*& L);

		int functionRef = -1;
		bool usingFallbackScript = false;
		juce::String script;
		juce::String fallbackScript;
		std::atomic<bool> updateVariables = false;
		std::function<void(int, juce::String, juce::String)> errorCallback;
		juce::String fileName;
		std::vector<lua_State*> seenStates;
	};