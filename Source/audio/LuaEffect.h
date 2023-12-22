#pragma once
#include "EffectApplication.h"
#include "../shape/Vector2.h"
#include "../audio/Effect.h"
#include "../PluginProcessor.h"

class LuaEffect : public EffectApplication {
public:
	LuaEffect(juce::String name, OscirenderAudioProcessor& p) : audioProcessor(p), name(name) {};

	Vector2 apply(int index, Vector2 input, const std::vector<double>& values, double sampleRate) override;
private:
	OscirenderAudioProcessor& audioProcessor;
	juce::String name;
};