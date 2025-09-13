#pragma once
#include <JuceHeader.h>

class GodRayEffect : public osci::EffectApplication {
public:
    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>> &values, double sampleRate) override {
        double noiseAmp = juce::jmax(0.0, values[0].load());
        double bias = values[1];
        double biasExponent = std::pow(12.0, std::abs(bias));

        double noise = (double)std::rand() / RAND_MAX;
        // Bias values toward 0 or 1 based on sign
        if (bias > 0.0) {
            noise = std::pow(noise, biasExponent);
        }
        else {
            noise = 1 - std::pow(1 - noise, biasExponent);
        }
        double scale = (1 - noiseAmp) + noise * noiseAmp;
        return input * scale;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<GodRayEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("God Ray Strength",
                                          "Creates a god ray effect by adding noise. This slider controls the size of the rays. Looks best with higher sample rates.",
                                          "godRayStrength", VERSION_HINT, 0.5, 0.0, 1.0),
                new osci::EffectParameter("God Ray Position",
                                          "Controls whether the rays appear to be radiating inward or outward.",
                                          "godRayPosition", VERSION_HINT, 0.8, -1.0, 1.0)
        });
        eff->setName("God Ray");
        eff->setIcon(BinaryData::god_ray_svg);
        return eff;
    }
};