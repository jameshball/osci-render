#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class HarmonicDuplicatorEffect : public osci::EffectApplication {
public:
    HarmonicDuplicatorEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        const double twoPi = juce::MathConstants<double>::twoPi;
        double copies = juce::jmax(1.0, values[0].load());
        double ceilCopies = std::ceil(copies - 1e-3);
        double spread = juce::jlimit(0.0, 1.0, values[1].load());
        double angleOffset = values[2].load() * juce::MathConstants<double>::twoPi;

        // Offset moves each time the input shape is drawn once
        double theta = std::floor(framePhase * copies) / copies * twoPi + angleOffset;
        osci::Point offset(std::cos(theta), std::sin(theta), 0.0);

        framePhase += audioProcessor.frequency / ceilCopies / sampleRate;
        framePhase = framePhase - std::floor(framePhase);

        return (1 - spread) * input + spread * offset;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<HarmonicDuplicatorEffect>(audioProcessor),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Copies",
                                          "Controls the number of copies of the input shape to draw. Splitting the shape into multiple copies creates audible harmony.",
                                          "harmonicDuplicatorCopies", VERSION_HINT, 3.0, 1.0, 6.0),
                new osci::EffectParameter("Spread",
                                          "Controls the spread between copies of the input shape.",
                                          "harmonicDuplicatorSpread", VERSION_HINT, 0.4, 0.0, 1.0),
                new osci::EffectParameter("Angle Offset",
                                          "Rotates the offsets between copies without rotating the input shape.",
                                          "harmonicDuplicatorAngle", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.1)
            }
        );
        eff->setName("Harmonic Duplicator");
        eff->setIcon(BinaryData::kaleidoscope_svg);
        return eff;
    }

private:
    OscirenderAudioProcessor &audioProcessor;
    double framePhase = 0.0; // [0, 1]
};