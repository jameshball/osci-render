#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class DashedLineEffect : public osci::EffectApplication {
public:
	DashedLineEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		// if only 2 parameters are provided, this is being used as a 'trace effect'
		// where the dash count is 1.
		double dashCount = 1.0;
		int i = 0;

		if (values.size() > 2) {
			dashCount = juce::jmax(1.0, values[i++].load()); // Dashes per cycle
		}

		double dashCoverage = juce::jlimit(0.0, 1.0, values[i++].load());
		double dashOffset = values[i++];
        
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

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<DashedLineEffect>(audioProcessor),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Dash Count", "Controls the number of dashed lines in the drawing.", "dashCount", VERSION_HINT, 16.0, 1.0, 32.0),
				new osci::EffectParameter("Dash Width", "Controls how much each dash unit is drawn.", "dashWidth", VERSION_HINT, 0.5, 0.0, 1.0),
				new osci::EffectParameter("Dash Offset", "Offsets the location of the dashed lines.", "dashOffset", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001f, osci::LfoType::Sawtooth, 1.0f),
			}
		);
		eff->setName("Dash");
		eff->setIcon(BinaryData::dash_svg);
		return eff;
	}

private:
	OscirenderAudioProcessor &audioProcessor;
	const static int MAX_BUFFER = 192000;
	std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_BUFFER);
	int bufferIndex = 0;
	double framePhase = 0.0; // [0, 1]
};
