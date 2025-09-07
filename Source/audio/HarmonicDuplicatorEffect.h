#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class HarmonicDuplicatorEffect : public osci::EffectApplication {
public:
    HarmonicDuplicatorEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        const double twoPi = juce::MathConstants<double>::twoPi;
        double copies = juce::jmax(1.0, values[0].load());
        double dist = juce::jlimit(0.0, 1.0, values[1].load());
        double angleOffset = values[2].load() * juce::MathConstants<double>::twoPi;

        double theta = std::floor(framePhase * copies) / copies * twoPi + angleOffset;
        osci::Point offset(std::cos(theta), std::sin(theta), 0.0);

        framePhase += audioProcessor.frequency / copies / sampleRate;
        framePhase = framePhase - std::floor(framePhase);

        return (1 - dist) * input + dist * offset;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<HarmonicDuplicatorEffect>(audioProcessor),
            std::vector<osci::EffectParameter*>{
                // TODO ID strings
                new osci::EffectParameter("Copies", "Controls the number of copies of the input shape to draw.", "rdCopies", VERSION_HINT, 3.0, 1.0, 10.0),
                new osci::EffectParameter("Distance", "Controls the distance between copies of the input shape.", "rdMix", VERSION_HINT, 0.5, 0.0, 1.0),
                new osci::EffectParameter("Angle Offset", "Rotates the offsets between copies without rotating the input shape.", "rdAngle", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 1.0)
        }
        );
        eff->setName("Radial Duplicator");
        eff->setIcon(BinaryData::kaleidoscope_svg);
        return eff;
    }

private:
    OscirenderAudioProcessor &audioProcessor;
    double framePhase = 0.0; // [0, 1]
};