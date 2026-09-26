#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace motion {
// Live playback and offline export use the same integer clock. Validate before
// casting: finite project times can still overflow when multiplied by the rate.
inline std::optional<std::int64_t> sampleIndex(double seconds, double sampleRate) {
    constexpr double largestExactIndex = 9007199254740991.0;
    if (!std::isfinite(seconds) || seconds < 0.0 || !std::isfinite(sampleRate) || sampleRate <= 0.0) {
        return std::nullopt;
    }
    const auto value = std::round(seconds * sampleRate);
    if (!std::isfinite(value) || value > largestExactIndex) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}
}
