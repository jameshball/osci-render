#pragma once
#include "EffectApplication.h"
#include "../shape/Point.h"
#include "../audio/Effect.h"
#include "../PluginProcessor.h"

class LuaEffect : public EffectApplication {
public:
	LuaEffect(juce::String name, OscirenderAudioProcessor& p) : audioProcessor(p), name(name) {};

	Point apply(int index, Point input, const std::vector<double>& values, double sampleRate) override;
private:
	OscirenderAudioProcessor& audioProcessor;
	juce::String name;
};