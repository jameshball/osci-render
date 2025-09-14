// BounceEffect.h (Simplified DVD-style 2D bounce)
// Scales the original shape, then translates it within [-1,1] x [-1,1] using
// constant-velocity motion that bounces off the edges. Z coordinate is unchanged.
#pragma once

#include <JuceHeader.h>

class BounceEffect : public osci::EffectApplication {
public:
    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        // values[0] = size (0.05..1.0)
        // values[1] = speed (0..)
        // values[2] = angle (0..1 -> 0..2π)
        double size = juce::jlimit(0.05, 1.0, values[0].load());
        double speed = values[1].load();
        double angle = values[2].load() * juce::MathConstants<double>::twoPi;

        // Base direction from user
        double dirX = std::cos(angle);
        double dirY = std::sin(angle);
        if (flipX) dirX = -dirX;
        if (flipY) dirY = -dirY;

        double dt = 1.0 / sampleRate;
        position.x += dirX * speed * dt;
        position.y += dirY * speed * dt;

        double maxP = 1.0 - size;
        double minP = -1.0 + size;
        if (position.x > maxP) { position.x = maxP; flipX = !flipX; }
        else if (position.x < minP) { position.x = minP; flipX = !flipX; }
        if (position.y > maxP) { position.y = maxP; flipY = !flipY; }
        else if (position.y < minP) { position.y = minP; flipY = !flipY; }

        osci::Point scaled = input * size;
        osci::Point out = scaled + osci::Point(position.x, position.y, 0.0);
        out.z = input.z; // preserve original Z
        return out;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<BounceEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Bounce Size", "Size (scale) of the bouncing object.", "bounceSize", VERSION_HINT, 0.3, 0.05, 1.0),
                new osci::EffectParameter("Bounce Speed", "Speed of motion.", "bounceSpeed", VERSION_HINT, 5.0, 0.0, 10.0),
                new osci::EffectParameter("Bounce Angle", juce::String(juce::CharPointer_UTF8("Direction of travel (0..1 -> 0..360°).")), "bounceAngle", VERSION_HINT, 0.16, 0.0, 1.0),
            }
        );
        eff->setName("Bounce");
        eff->setIcon(BinaryData::bounce_svg);
        return eff;
    }

private:
    osci::Point position { 0.0, 0.0, 0.0 };
    bool flipX = false;
    bool flipY = false;
};
