#pragma once
#include <JuceHeader.h>

class SpiralBitCrushEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>> &values, double sampleRate) override {
		// Completing one revolution in input space traverses the hypotenuse of one "domain" in log-polar space
		double effectScale = juce::jlimit(0.0, 1.0, values[0].load());
		double domainX = juce::jmax(2.0, std::floor(values[1].load() + 0.0001));
		double domainY = std::round(domainX * values[2].load());
		osci::Point offset(values[3].load(), values[4].load());

		double domainHypot = std::hypot(domainX, domainY);
		double domainTheta = std::atan2(domainY, domainX);
		double scale = domainHypot / juce::MathConstants<double>::twoPi;
		osci::Point output(0);

		// Round to log-polar grid
		if (input.x != 0 || input.y != 0) {
			// Convert input point to log-polar coordinates transformed based on domain and offset
			// Note 90 degree rotation: Theta is treated relative to -Y rather than +X
			double r = std::hypot(input.x, input.y);
			double logR = std::log(r);
			double theta = std::atan2(input.x, -input.y);
			osci::Point logPolarCoords(theta, logR);
			logPolarCoords.rotate(0, 0, domainTheta);
			logPolarCoords = logPolarCoords * scale - offset;

			// Round this point to the center of the log-polar cell the input lies in, convert back to Cartesian
			logPolarCoords.x = std::round(logPolarCoords.x);
			logPolarCoords.y = std::round(logPolarCoords.y);
			logPolarCoords = (logPolarCoords + offset) / scale;
			logPolarCoords.rotate(0, 0, -domainTheta);
			double outR = std::exp(logPolarCoords.y);
			double outTheta = logPolarCoords.x;
			output.x = outR *  std::sin(outTheta);
			output.y = outR * -std::cos(outTheta);
		}

		// Round z in log space using same spacing as xy log-polar grid
		// Use same offset as xy's radial offset to be consistent with the appearance of zooming
		if (input.z != 0) {
			double signZ = input.z > 0 ? 1.0 : -1.0;
			double logZ = std::log(std::abs(input.z));
			logZ = logZ * scale - offset.y;
			logZ = std::round(logZ);
			logZ = (logZ + offset.y) / scale;
			output.z = signZ * std::exp(logZ);
		}
		return (1 - effectScale) * input + effectScale * output;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<SpiralBitCrushEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Spiral Bit Crush", "Constrains points to a spiral pattern.", "spiralBitCrushEnable", VERSION_HINT, 1.0, 0.0, 1.0),
				new osci::EffectParameter("Spiral Density", "Controls the density of the spiral pattern.", "spiralDensity", VERSION_HINT, 13.0, 3.0, 30.0),
				new osci::EffectParameter("Spiral Twist", "Controls how much the spiral pattern twists.", "spiralTwist", VERSION_HINT, -0.6, -1.0, 1.0),
				new osci::EffectParameter("Angle Offset", "Rotates the spiral pattern.", "spiralOffsetX", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.4),
				new osci::EffectParameter("Radial Offset", "Zooms the spiral pattern.", "spiralOffsetY", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 1.0)
		});
		eff->setIcon(BinaryData::swirl_svg);
		return eff;
	}
};