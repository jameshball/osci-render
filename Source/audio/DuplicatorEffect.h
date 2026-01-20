#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class DuplicatorEffect : public osci::EffectApplication {
public:
    std::shared_ptr<osci::EffectApplication> clone() const override {
        return std::make_shared<DuplicatorEffect>();
    }

    osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override {
        const double twoPi = juce::MathConstants<double>::twoPi;
        double copies = juce::jmax(1.0f, values[0].load());
        double spread = juce::jlimit(0.0f, 1.0f, values[1].load());
        double angleOffset = values[2].load() * juce::MathConstants<double>::twoPi;

        // Offset moves each time the input shape is drawn once
        double theta = std::floor(framePhase * copies) / copies * twoPi + angleOffset;
        osci::Point offset(std::cos(theta), std::sin(theta), 0.0);

        double freqDivisor = std::ceil(copies - 1e-3);
        framePhase += frequency / freqDivisor / sampleRate;
        framePhase = framePhase - std::floor(framePhase);

        return (1 - spread) * input + spread * offset;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<DuplicatorEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Duplicator Copies",
                                          "Controls the number of copies of the input shape to draw. Splitting the shape into multiple copies creates audible harmony.",
                                          "duplicatorCopies", VERSION_HINT, 3.0, 1.0, 6.0),
                new osci::EffectParameter("Spread",
                                          "Controls the spread between copies of the input shape.",
                                          "duplicatorSpread", VERSION_HINT, 0.4, 0.0, 1.0),
                new osci::EffectParameter("Angle Offset",
                                          "Rotates the offsets between copies without rotating the input shape.",
                                          "duplicatorAngle", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.1)
            }
        );
        eff->setName("Duplicator");
        eff->setIcon(BinaryData::duplicator_svg);
        return eff;
    }

private:
    double framePhase = 0.0; // [0, 1]
};
