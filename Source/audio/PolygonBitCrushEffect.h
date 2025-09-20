#pragma once
#include <JuceHeader.h>
#include "../MathUtil.h"

// Inspired by xenontesla122
class PolygonBitCrushEffect : public osci::EffectApplication {
public:
    osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>&values, float sampleRate) override {
        const double pi = juce::MathConstants<double>::pi;
        const double twoPi = juce::MathConstants<double>::twoPi;
        double effectScale = juce::jlimit(0.0f, 1.0f, values[0].load());
        double nSides = juce::jmax(2.0f, values[1].load());
        double stripeSize = juce::jmax(1e-4f, values[2].load());
        stripeSize = std::pow(0.63 * stripeSize, 1.5); // Change range and bias toward smaller values
        double rotation = values[3] * twoPi;
        double stripePhase = values[4];

        osci::Point output(0);
        if (input.x != 0 || input.y != 0) {
            // Note 90 degree rotation: Theta is treated relative to +Y rather than +X
            double r = std::hypot(input.x, input.y);
            double theta = std::atan2(-input.x, input.y) - rotation;
            theta = MathUtil::wrapAngle(theta + pi) - pi; // Move branch cut to +/-pi after angle offset is applied
            double regionCenterTheta = std::round(theta * nSides / twoPi) / nSides * twoPi;
            double localTheta = theta - regionCenterTheta;
            double dist = r * juce::dsp::FastMathApproximations::cos(localTheta);
            double newDist = juce::jmax(0.0, (std::round(dist / stripeSize - stripePhase) + stripePhase) * stripeSize);
            double scale = newDist / dist;
            output.x = scale * input.x;
            output.y = scale * input.y;
        }
        // Apply the same stripe quantization to abs(z) if the input is 3D
        double absZ = std::abs(input.z);
        if (absZ > 0.0001) {
            double signZ = input.z > 0 ? 1 : -1;
            output.z = signZ * juce::jmax(0.0, (std::round(absZ / stripeSize - stripePhase) + stripePhase) * stripeSize);
        }
        return (1 - effectScale) * input + effectScale * output;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<PolygonBitCrushEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Polygon Bit Crush",
                                          "Constrains points to a polygon pattern.",
                                          "polygonBitCrush", VERSION_HINT, 1.0, 0.0, 1.0),
                new osci::EffectParameter("Sides", "Controls the number of sides of the polygon pattern.",
                                          "polygonBitCrushSides", VERSION_HINT, 5.0, 3.0, 8.0),
                new osci::EffectParameter("Stripe Size",
                                          "Controls the spacing between the stripes of the polygon pattern.",
                                          "polygonBCStripeSize", VERSION_HINT, 0.5, 0.0, 1.0),
                new osci::EffectParameter("Rotation", "Rotates the polygon pattern.",
                                          "polygonBCRotation", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.1),
                new osci::EffectParameter("Stripe Phase", "Offsets the stripes of the polygon pattern.",
                                          "polygonBCStripePhase", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 2.0)
            }
        );
        eff->setIcon(BinaryData::polygon_bitcrush_svg);
        return eff;
    }
};
