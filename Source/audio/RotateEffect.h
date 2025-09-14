#pragma once
#include <JuceHeader.h>

class RotateEffectApp : public osci::EffectApplication {
public:
    osci::Point apply(int /*index*/, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate) override {
        input.rotate(values[0] * std::numbers::pi, values[1] * std::numbers::pi, values[2] * std::numbers::pi);
        return input;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<RotateEffectApp>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Rotate X", "Controls the rotation of the object in the X axis.", "rotateX", VERSION_HINT, 0.0, -1.0, 1.0),
                new osci::EffectParameter("Rotate Y", "Controls the rotation of the object in the Y axis.", "rotateY", VERSION_HINT, 0.0, -1.0, 1.0, 0.0001f, osci::LfoType::Sawtooth, 0.2f),
                new osci::EffectParameter("Rotate Z", "Controls the rotation of the object in the Z axis.", "rotateZ", VERSION_HINT, 0.0, -1.0, 1.0),
            }
        );
        eff->setName("Rotate");
        eff->setIcon(BinaryData::rotate_svg);
        return eff;
    }
};
