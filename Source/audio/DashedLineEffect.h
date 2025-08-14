#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class DashedLineEffect : public osci::EffectApplication {
public:
	DashedLineEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double dashCount = juce::jmax(1.0, values[0].load()); // Dashes per cycle
		double dashCoverage = juce::jlimit(0.0, 1.0, values[1].load());
		double dashOffset = values[2];
		double dashLengthSamples = (sampleRate / audioProcessor.frequency) / dashCount;
		double dashPhase = framePhase * dashCount - dashOffset;
		dashPhase = dashPhase - std::floor(dashPhase); // Wrap
		buffer[bufferIndex] = input;

		// Linear interpolation works much better than nearest for this
		double samplePos = bufferIndex - dashLengthSamples * dashPhase * (1 - dashCoverage);
		samplePos = samplePos - buffer.size() * std::floor(samplePos / buffer.size()); // Wrap to [0, size]
		int lowIndex = (int)std::floor(samplePos) % buffer.size();
		int highIndex = (lowIndex + 1) % buffer.size();
		double mixFactor = samplePos - std::floor(samplePos); // Fractional part
		osci::Point output = (1 - mixFactor) * buffer[lowIndex] + mixFactor * buffer[highIndex];

		bufferIndex++;
		if (bufferIndex >= buffer.size()) {
			bufferIndex = 0;
		}
		framePhase += audioProcessor.frequency / sampleRate;
		framePhase = framePhase - std::floor(framePhase);

		return output;
	}

private:
	OscirenderAudioProcessor &audioProcessor;
	const static int MAX_BUFFER = 192000;
	std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_BUFFER);
	int bufferIndex = 0;
	double framePhase = 0.0; // [0, 1]
};