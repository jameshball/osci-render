#pragma once
#include <JuceHeader.h>

class RippleEffectApp : public osci::EffectApplication {
public:
    osci::Point apply(int /*index*/, osci::Point input, const std::vector<std::atomic<double>>& values, double /*sampleRate*/) override {
        double phase = values[1] * std::numbers::pi;
        double distance = 100 * values[2] * (input.x * input.x + input.y * input.y);
        input.z += values[0] * std::sin(phase + distance);
        return input;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<RippleEffectApp>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Ripple Depth", "Controls how large the ripples applied to the image are.", "rippleDepth", VERSION_HINT, 0.2, 0.0, 1.0),
                new osci::EffectParameter("Ripple Phase", "Controls the position of the ripple. Animate this to see a moving ripple effect.", "ripplePhase", VERSION_HINT, 0.0, -1.0, 1.0, 0.0001f, osci::LfoType::Sawtooth, 1.0f),
                new osci::EffectParameter("Ripple Amount", "Controls how many ripples are applied to the image.", "rippleAmount", VERSION_HINT, 0.1, 0.0, 1.0),
            }
        );
        eff->setName("Ripple");
        eff->setIcon(BinaryData::ripple_svg);
        return eff;
    }
};
