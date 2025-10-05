#pragma once
#include <JuceHeader.h>

class SpiralBitCrushEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>&values, float sampleRate) override {
		// Completing one revolution in input space traverses the hypotenuse of one "domain" in log-polar space
        double effectScale = juce::jlimit(0.0f, 1.0f, values[0].load());
		double domainX = juce::jmax(2.0, std::floor(values[1].load() + 0.001));
		double domainY = std::round(domainX * values[2].load());
        double zoom = values[3].load() * juce::MathConstants<double>::twoPi; // Use same scale as angle
        double rotation = values[4].load() * juce::MathConstants<double>::twoPi;
        
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
			osci::Point logPolarCoords(theta - rotation, logR - zoom);
			logPolarCoords.rotate(0, 0, domainTheta);
			logPolarCoords = logPolarCoords * scale;

			// Round this point to the center of the log-polar cell the input lies in, convert back to Cartesian
			logPolarCoords.x = std::round(logPolarCoords.x);
			logPolarCoords.y = std::round(logPolarCoords.y);
			logPolarCoords = logPolarCoords / scale;
			logPolarCoords.rotate(0, 0, -domainTheta);
			double outR = std::exp(logPolarCoords.y + zoom);
			double outTheta = logPolarCoords.x + rotation;
			output.x = outR *  std::sin(outTheta);
			output.y = outR * -std::cos(outTheta);
		}

		// Round z in log space using same spacing as xy log-polar grid
		// Use same offset as xy's radial offset to be consistent with the appearance of zooming
		if (input.z != 0) {
			double signZ = input.z > 0 ? 1.0 : -1.0;
			double logZ = std::log(std::abs(input.z));
			logZ = (logZ - zoom) * scale;
			logZ = std::round(logZ);
            logZ = logZ / scale + zoom;
			output.z = signZ * std::exp(logZ);
		}
		return (1 - effectScale) * input + effectScale * output;
	}

	std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<SpiralBitCrushEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Spiral Bit Crush",
                                          "Constrains points to a spiral pattern.",
                                          "spiralBitCrush", VERSION_HINT, 0.4, 0.0, 1.0),
                new osci::EffectParameter("Spiral Density",
                                          "Controls the density of the spiral pattern.",
                                          "spiralBitCrushDensity", VERSION_HINT, 13.0, 3.0, 30.0, 1.0),
                new osci::EffectParameter("Spiral Twist",
                                          "Controls how much the spiral pattern twists.",
                                          "spiralBitCrushTwist", VERSION_HINT, 0.6, -1.0, 1.0),
                new osci::EffectParameter("Zoom",
                                          "Zooms the spiral pattern.",
                                          "spiralBitCrushZoom", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.1),
                new osci::EffectParameter("Rotation",
                                          "Rotates the spiral pattern.",
                                          "spiralBitCrushRotation", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::ReverseSawtooth, 0.02)
            }
        );
		eff->setIcon(BinaryData::spiral_bitcrush_svg);
		return eff;
	}
};
