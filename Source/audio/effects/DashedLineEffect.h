#pragma once
#include <JuceHeader.h>

class DashedLineEffect : public osci::EffectApplication {
public:
	std::shared_ptr<osci::EffectApplication> clone() const override {
		return std::make_shared<DashedLineEffect>();
	}

	void onFrameStart() override {
		bufferIndex = 0;
		samplesSinceFrameStart = 0;
	}

	osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override {
		// if only 2 parameters are provided, this is being used as a 'trace effect'
		// where the dash count is 1.
		double dashCount = 1.0;
		int i = 0;

		if (values.size() > 2) {
			dashCount = juce::jmax(1.0f, values[i++].load()); // Dashes per cycle
		}

		double dashOffset = values[i++].load();
		double dashCoverage = juce::jlimit(0.0f, 1.0f, values[i++].load());

		const double safeFrequency = juce::jmax(1.0, (double)frequency);
		const int cycleSamples = juce::jlimit(1, MAX_BUFFER, (int)std::ceil(sampleRate / safeFrequency));

		// Fallback for contexts where we don't get explicit frame sync pulses
		if (samplesSinceFrameStart >= cycleSamples) {
			samplesSinceFrameStart = 0;
			bufferIndex = 0;
		}

		const double framePhase01 = (double)samplesSinceFrameStart / (double)cycleSamples; // [0, 1)
		const double dashLengthSamples = ((double)cycleSamples / dashCount);
		double dashPhase = framePhase01 * dashCount - dashOffset;
		dashPhase = dashPhase - std::floor(dashPhase); // Wrap

		buffer[bufferIndex] = input;

		// Linear interpolation works much better than nearest for this
		double samplePos = bufferIndex - dashLengthSamples * dashPhase * (1.0 - dashCoverage);
		samplePos = samplePos - cycleSamples * std::floor(samplePos / (double)cycleSamples); // Wrap to [0, cycleSamples)
		int lowIndex = (int)std::floor(samplePos) % cycleSamples;
		int highIndex = (lowIndex + 1) % cycleSamples;
		double mixFactor = samplePos - std::floor(samplePos); // Fractional part
		osci::Point output = (1 - mixFactor) * buffer[lowIndex] + mixFactor * buffer[highIndex];

		bufferIndex++;
		if (bufferIndex >= cycleSamples) {
			bufferIndex = 0;
		}
		samplesSinceFrameStart++;

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
	int samplesSinceFrameStart = 0;
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
