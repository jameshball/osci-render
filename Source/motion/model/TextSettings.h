#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <numbers>

namespace motion {
struct TextSettings {
    juce::String family;
    int style = juce::Font::plain;
    int alignment = 0; // Left, centre, right; explicit line breaks are preserved.
    double lineSpacing = 1.2;
    double tracking = 0.0; // Additional advance in em units.
    // Per-character animation, baked into frames at 30 fps. Each visible
    // character starts `characterDelay` seconds after the previous one and
    // takes `characterDuration`; the last pose then holds for `hold` seconds
    // before the source loops. `amount` scales the offsets (1 = one em).
    enum class Animation { none, typeOn, rise, pop, wave, scatter };
    Animation animation = Animation::none;
    double characterDelay = 0.06, characterDuration = 0.35, hold = 1.5, amount = 1.0;

    bool operator==(const TextSettings&) const = default;
    bool animated() const { return animation != Animation::none; }
    juce::String validate() const {
        if (family.length() > 256) { return "Font family name is too long."; }
        if (style < 0 || style > (juce::Font::bold | juce::Font::italic)) { return "Invalid font style."; }
        if (alignment < 0 || alignment > 2) { return "Invalid text alignment."; }
        if (!std::isfinite(lineSpacing) || lineSpacing < 0.5 || lineSpacing > 4) { return "Line spacing must be between 0.5 and 4 em."; }
        if (!std::isfinite(tracking) || tracking < -0.2 || tracking > 1) { return "Tracking must be between -0.2 and 1 em."; }
        if (static_cast<int>(animation) < 0 || static_cast<int>(animation) > 5) { return "Unknown text animation."; }
        const auto seconds = [](double value) { return std::isfinite(value) && value >= 0 && value <= 30; };
        if (!seconds(characterDelay) || !seconds(hold) || !std::isfinite(characterDuration) || characterDuration < 0.01 || characterDuration > 30) {
            return "Text animation timing must be 0-30 seconds (at least 0.01 s per character).";
        }
        if (!std::isfinite(amount) || amount < -10 || amount > 10) { return "Text animation amount must be between -10 and 10."; }
        return {};
    }
    // Seconds from the first character's start to the end of the hold.
    double animationLength(int characters) const {
        if (animation == Animation::wave) { return characterDuration; }
        return characterDelay * std::max(0, characters - 1) + characterDuration + hold;
    }
    // One character's pose at time t: visibility, offset (em), scale and
    // rotation (degrees). index counts visible characters.
    struct Pose { bool visible = true; double x = 0, y = 0, scale = 1, rotation = 0; };
    Pose pose(int index, double t) const {
        const auto started = t - characterDelay * index;
        const auto u = std::clamp(started / characterDuration, 0.0, 1.0);
        const auto ease = 1 - (1 - u) * (1 - u) * (1 - u); // ease-out cubic
        Pose result;
        switch (animation) {
            case Animation::none: break;
            case Animation::typeOn: result.visible = started >= 0; break;
            case Animation::rise:
                result.visible = started >= 0;
                result.y = -(1 - ease) * amount;
                break;
            case Animation::pop: {
                result.visible = started >= 0;
                // Overshoots to 1.2x, then settles (easeOutBack).
                const auto c = 1.70158 * amount, v = u - 1;
                result.scale = std::max(0.0, 1 + (c + 1) * v * v * v + c * v * v);
                break;
            }
            case Animation::wave: {
                const auto phase = 2 * std::numbers::pi * (t / characterDuration - characterDelay * index / characterDuration);
                result.y = 0.25 * amount * std::sin(phase);
                break;
            }
            case Animation::scatter: {
                // Characters fly in from a fixed pseudo-random direction.
                const auto angle = std::fmod(index * 2.399963229728653, 2 * std::numbers::pi);
                result.visible = started >= 0;
                result.x = std::cos(angle) * (1 - ease) * 2 * amount;
                result.y = std::sin(angle) * (1 - ease) * 2 * amount;
                result.rotation = (1 - ease) * 180 * amount;
                break;
            }
        }
        return result;
    }
    juce::Font font(float height) const {
        auto options = juce::FontOptions(height, style);
        if (family.isNotEmpty()) { options = options.withName(family); }
        return juce::Font(options).withExtraKerningFactor(static_cast<float>(tracking));
    }
};
}
