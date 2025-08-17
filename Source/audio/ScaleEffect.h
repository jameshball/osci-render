#pragma once
#include <JuceHeader.h>

class ScaleEffectApp : public osci::EffectApplication {
public:
    osci::Point apply(int /*index*/, osci::Point input, const std::vector<std::atomic<double>>& values, double /*sampleRate*/) override {
        return input * osci::Point(values[0], values[1], values[2]);
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<ScaleEffectApp>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Scale X", "Scales the object in the horizontal direction.", "scaleX", VERSION_HINT, 1.2, -3.0, 3.0),
                new osci::EffectParameter("Scale Y", "Scales the object in the vertical direction.", "scaleY", VERSION_HINT, 1.2, -3.0, 3.0),
                new osci::EffectParameter("Scale Z", "Scales the depth of the object.", "scaleZ", VERSION_HINT, 1.2, -3.0, 3.0),
            }
        );
        eff->setName("Scale");
        eff->setIcon(BinaryData::scale_svg);
        eff->markLockable(true);
        return eff;
    }
};
