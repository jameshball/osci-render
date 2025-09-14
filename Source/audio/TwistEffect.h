#pragma once
#include <JuceHeader.h>
#include <numbers>

class TwistEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<float>>&values, float sampleRate) override {
		double twistStrength = values[0] * 4 * std::numbers::pi;
		double twistTheta = twistStrength * input.y;
		input.rotate(0.0, twistTheta, 0.0);
		return input;
	}
    
    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<TwistEffect>(),
            new osci::EffectParameter("Twist", "Twists the image in a corkscrew pattern.", "twist", VERSION_HINT, 0.5, 0.0, 1.0, 0.0001, osci::LfoType::Sine, 0.5)
        );
        eff->setIcon(BinaryData::twist_svg);
        return eff;
    }
};
