#pragma once
#include <JuceHeader.h>
#include "../MathUtil.h"

// Inspired by xenontesla122
class PolygonBitCrushEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>> &values, double sampleRate) override {
		const double pi = juce::MathConstants<double>::pi;
		const double twoPi = juce::MathConstants<double>::twoPi;
		double effectScale = juce::jlimit(0.0, 1.0, values[0].load());
		double nSides = juce::jmax(2.0, values[1].load());
		double bandSize = juce::jmax(1e-4, values[2].load());
		double thetaOffset = values[3] * twoPi;
		double rOffset = values[4];

		osci::Point output(0);
		if (input.x != 0 || input.y != 0) {
			// Note 90 degree rotation: Theta is treated relative to -Y rather than +X
			double r = std::hypot(input.x, input.y);
			double theta = std::atan2(-input.x, input.y) - thetaOffset;
			theta = MathUtil::wrapAngle(theta + pi) - pi; // Move branch cut to +/-pi after thetaOffset is applied
			double regionTheta = std::round(theta * nSides / twoPi) / nSides * twoPi;
			double localTheta = theta - regionTheta;
			double dist = r * std::cos(localTheta);
			double newDist = juce::jmax(0.0, (std::round(dist / bandSize - rOffset) + rOffset) * bandSize);
			double scale = newDist / dist;
			output.x = scale * input.x;
			output.y = scale * input.y;
		}
		// Apply the same stripe quantization to abs(z)
		if (input.z != 0) {
			double signZ = input.z > 0 ? 1 : -1;
			output.z = signZ * juce::jmax(0.0, (std::round(std::abs(input.z) / bandSize - rOffset) + rOffset) * bandSize);
		}
		return (1 - effectScale) * input + effectScale * output;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<PolygonBitCrushEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Polygon Bit Crush", "Constrains points to a polygon pattern.", "polygonBitCrushEnable", VERSION_HINT, 1.0, 0.0, 1.0),
				new osci::EffectParameter("Sides", "Controls the number of sides of the polygon pattern.", "polygonSides", VERSION_HINT, 5.0, 3.0, 8.0),
				new osci::EffectParameter("Stripe Size", "Controls the spacing between the stripes of the polygon pattern.", "polygonBandSize", VERSION_HINT, 0.15, 0.0, 0.5),
				new osci::EffectParameter("Angle Offset", "Rotates the polygon pattern.", "polygonAngleOffset", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.1),
				new osci::EffectParameter("Radial Offset", "Offsets the stripes of the polygon pattern.", "polygonROffset", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 2.0)
		});
		eff->setIcon(BinaryData::diamond_svg);
		return eff;
	}
};