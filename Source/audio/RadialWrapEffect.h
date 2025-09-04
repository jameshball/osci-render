#pragma once
#include <JuceHeader.h>

class RadialWrapEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>> &values, double sampleRate) override {
		// Treat input as complex number and raise to integer power
		// Disallowing non-integer and negative exponents because of the branch cut
		// Note 90 degree rotation: Theta is treated relative to +Y rather than +X
		double effectScale = juce::jlimit(0.0, 1.0, values[0].load());
		double exponent = juce::jmax(1.0, std::floor(values[1].load() + 0.0001));

		osci::Point output(0, 0, input.z);
		if (input.x != 0 || input.y != 0) {
			double r2 = input.x * input.x + input.y * input.y;
			double theta = std::atan2(-input.x, input.y);

			double outR = std::pow(r2, 0.5 * exponent);
			double outTheta = exponent * theta;
			output.x = outR * -std::sin(outTheta);
			output.y = outR *  std::cos(outTheta);
		}
		return (1 - effectScale) * input + effectScale * output;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<RadialWrapEffect>(),
			std::vector<osci::EffectParameter *>{
				new osci::EffectParameter("Radial Wrap", "Distorts the shape by multiplying the angle and warping the length of each point.", "radialWrapEnable", VERSION_HINT, 1.0, 0.0, 1.0),
				new osci::EffectParameter("Wrap Degree", "The multiplier applied to each point's angle.", "radialWrapDegree", VERSION_HINT, 2.0, 1.0, 6.0)
		});
		eff->setIcon(BinaryData::twist_svg);
		return eff;
	}
};