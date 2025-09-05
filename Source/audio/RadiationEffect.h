#pragma once
#include <JuceHeader.h>

class RadiationEffect : public osci::EffectApplication {
public:
    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>> &values, double sampleRate) override {
        double noiseAmp = juce::jmax(0.0, values[0].load());
        double bias = values[1];
        double biasExponent = std::pow(8.0, std::abs(bias));

        // TODO: Try lopsided radiation

        /*
        double noise = 2.0 * (double)std::rand() / RAND_MAX - 1.0;
        double noiseSign = noise >= 0.0 ? 1.0 : -1.0;
        // If bias is positive, bend values toward center
        // If bias is negative, bend values toward extremes
        if (bias < 0.0) {
            noise = noiseSign * (1 - std::pow(1 - std::abs(noise), biasExponent));
        }
        else {
            noise = noiseSign * std::pow(std::abs(noise), biasExponent);
        }
        noise = 0.5 + 0.5 * noise;

        */
        double noise = (double)std::rand() / RAND_MAX;
        //double noiseSign = noise >= 0.0 ? 1.0 : -1.0;
        // If bias is positive, bend values toward center
        // If bias is negative, bend values toward extremes
        if (bias < 0.0) {
            noise = 1 - std::pow(1 - noise, biasExponent);
        }
        else {
            noise = std::pow(noise, biasExponent);
        }

        double scale = (1 - noiseAmp) + noise * noiseAmp;
        return input * scale;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<RadiationEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Radiation", "Creates a crepuscular ray effect by adding noise. This slider controls the size of the rays.", "radiationAmp", VERSION_HINT, 1.0, 0.0, 1.0),
                new osci::EffectParameter("Radiation Bias", "Controls whether the rays appear to be radiating inward or outward.", "radiationBias", VERSION_HINT, 0.6, -1.0, 1.0)
        });
        eff->setIcon(BinaryData::multiplex_svg);
        return eff;
    }
};