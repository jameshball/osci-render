#pragma once
#include <JuceHeader.h>

class TranslateEffectApp : public osci::EffectApplication {
public:
    osci::Point apply(int /*index*/, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate) override {
        return input + osci::Point(values[0], values[1], values[2]);
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<TranslateEffectApp>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Translate X", "Moves the object horizontally.", "translateX", VERSION_HINT, 0.3, -1.0, 1.0),
                new osci::EffectParameter("Translate Y", "Moves the object vertically.", "translateY", VERSION_HINT, 0.0, -1.0, 1.0),
                new osci::EffectParameter("Translate Z", "Moves the object away from the camera.", "translateZ", VERSION_HINT, 0.0, -1.0, 1.0),
            }
        );
        eff->setName("Translate");
        eff->setIcon(BinaryData::translate_svg);
        return eff;
    }
};
