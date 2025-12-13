#pragma once
#include <JuceHeader.h>

class DashedLineEffect : public osci::EffectApplication {
public:
	std::shared_ptr<osci::EffectApplication> clone() const override {
		return std::make_shared<DashedLineEffect>();
	}

	osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override {
		// if only 2 parameters are provided, this is being used as a 'trace effect'
		// where the dash count is 1.
		double dashCount = 1.0;
		int i = 0;

		if (values.size() > 2) {
			dashCount = juce::jmax(1.0f, values[i++].load()); // Dashes per cycle
		}

		double dashOffset = values[i++];
		double dashCoverage = juce::jlimit(0.0f, 1.0f, values[i++].load());
        
		double dashLengthSamples = (sampleRate / frequency) / dashCount;
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
		framePhase += frequency / sampleRate;
		framePhase = framePhase - std::floor(framePhase);

		return output;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<DashedLineEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Dash Count", "Controls the number of dashed lines in the drawing.", "dashCount", VERSION_HINT, 16.0, 1.0, 32.0),
				new osci::EffectParameter("Dash Offset", "Offsets the location of the dashed lines.", "dashOffset", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001f, osci::LfoType::Sawtooth, 1.0f),
				new osci::EffectParameter("Dash Width", "Controls how much each dash unit is drawn.", "dashWidth", VERSION_HINT, 0.5, 0.0, 1.0),
			}
		);
		eff->setName("Dash");
		eff->setIcon(BinaryData::dash_svg);
		return eff;
	}

private:
	const static int MAX_BUFFER = 192000;
	std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_BUFFER);
	int bufferIndex = 0;
	double framePhase = 0.0; // [0, 1]
};

class TraceEffect : public DashedLineEffect {
public:
	TraceEffect() : DashedLineEffect() {}

	std::shared_ptr<osci::EffectApplication> clone() const override {
		return std::make_shared<TraceEffect>();
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<TraceEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter(
					"Trace Start",
					"Defines how far into the frame the drawing is started at. This has the effect of 'tracing' out the image from a single dot when animated. By default, we start drawing from the beginning of the frame, so this value is 0.0.",
					"traceStart",
					VERSION_HINT, 0.0, 0.0, 1.0, 0.001
				),
				new osci::EffectParameter(
					"Trace Length",
					"Defines how much of the frame is drawn per cycle. This has the effect of 'tracing' out the image from a single dot when animated. By default, we draw the whole frame, corresponding to a value of 1.0.",
					"traceLength",
					VERSION_HINT, 1.0, 0.0, 1.0, 0.001, osci::LfoType::Sawtooth
				)
			}
		);
		eff->setName("Trace");
		eff->setIcon(BinaryData::trace_svg);
		return eff;
	}
};
