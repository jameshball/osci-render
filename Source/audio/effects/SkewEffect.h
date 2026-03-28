#pragma once
#include <JuceHeader.h>
#include <numbers>

// Simple shear (skew) along each axis: X += skewX * Y, Y += skewY * Z, Z += skewZ * X
// Useful subtle perspective-like distortion that can be layered with scale/rotate.
class SkewEffect : public osci::EffectApplication {
public:
    SkewEffect() {}

    std::shared_ptr<osci::EffectApplication> clone() const override {
        return std::make_shared<SkewEffect>();
    }

    osci::Point apply(int /*index*/, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override {
        jassert(values.size() == 3);
        double tx = values[0].load(); // skew X by Y
        double ty = values[1].load(); // skew Y by Z
        double tz = values[2].load(); // skew Z by X

        // Apply sequential shears; keep original components where appropriate to avoid compounding order surprises.
        osci::Point out = input;
        out.x += tx * input.y; // shear X by Y
        out.y += ty * input.z; // shear Y by Z
        out.z += tz * input.x; // shear Z by X
        return out;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<SkewEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Skew X", "Skews (shears) the shape horizontally based on vertical position.", "skewX", VERSION_HINT, 0.0, -1.0, 1.0, 0.0001f, osci::LfoType::Sine, 0.2f),
                new osci::EffectParameter("Skew Y", "Skews (shears) the shape vertically based on depth.", "skewY", VERSION_HINT, 0.0, -1.0, 1.0),
                new osci::EffectParameter("Skew Z", "Skews (shears) the shape in depth based on horizontal position.", "skewZ", VERSION_HINT, 0.0, -1.0, 1.0),
            }
        );
        eff->setName("Skew");
        eff->setIcon(BinaryData::skew_svg);
        return eff;
    }
};
