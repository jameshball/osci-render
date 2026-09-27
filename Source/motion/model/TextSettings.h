#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace motion {
struct TextSettings {
    juce::String family;
    int style = juce::Font::plain;
    int alignment = 0; // Left, centre, right; explicit line breaks are preserved.
    double lineSpacing = 1.2;
    double tracking = 0.0; // Additional advance in em units.

    bool operator==(const TextSettings&) const = default;
    juce::String validate() const {
        if (family.length() > 256) { return "Font family name is too long."; }
        if (style < 0 || style > (juce::Font::bold | juce::Font::italic)) { return "Invalid font style."; }
        if (alignment < 0 || alignment > 2) { return "Invalid text alignment."; }
        if (!std::isfinite(lineSpacing) || lineSpacing < 0.5 || lineSpacing > 4) { return "Line spacing must be between 0.5 and 4 em."; }
        if (!std::isfinite(tracking) || tracking < -0.2 || tracking > 1) { return "Tracking must be between -0.2 and 1 em."; }
        return {};
    }
    juce::Font font(float height) const {
        auto options = juce::FontOptions(height, style);
        if (family.isNotEmpty()) { options = options.withName(family); }
        return juce::Font(options).withExtraKerningFactor(static_cast<float>(tracking));
    }
};
}
