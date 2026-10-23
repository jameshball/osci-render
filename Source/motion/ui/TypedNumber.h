#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace motion::ui {
// A typed number, finite and complete ("2x" is not 2), with an optional unit
// after it ("2.5 s" reads as 2.5 when `unit` is "s").
inline std::optional<double> parseNumber(juce::String text, juce::StringRef unit = {}) {
    text = text.trim();
    if (unit.isNotEmpty() && text.endsWithIgnoreCase(unit)) { text = text.dropLastCharacters(unit.length()).trim(); }
    // Plain decimal only: strtod also reads hex ("0x1F" as 31).
    if (text.isEmpty() || !text.containsOnly("0123456789+-.eE")) { return std::nullopt; }
    char* end = nullptr;
    const auto value = std::strtod(text.toRawUTF8(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(value)) { return std::nullopt; }
    return value;
}
}
