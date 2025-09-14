#pragma once
#include <JuceHeader.h>

class SwirlEffectApp : public osci::EffectApplication {
public:
    osci::Point apply(int /*index*/, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate) override {
        double length = 10 * values[0] * input.magnitude();
        double newX = input.x * juce::dsp::FastMathApproximations::cos(length) - input.y * juce::dsp::FastMathApproximations::sin(length);
        double newY = input.x * juce::dsp::FastMathApproximations::sin(length) + input.y * juce::dsp::FastMathApproximations::cos(length);
        return osci::Point(newX, newY, input.z);
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<SwirlEffectApp>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Swirl", "Swirls the image in a spiral pattern.", "swirl", VERSION_HINT, 0.4, -1.0, 1.0),
            }
        );
        eff->setIcon(BinaryData::swirl_svg);
        return eff;
    }
};
