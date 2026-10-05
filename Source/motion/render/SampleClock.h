#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace motion {
// The sample nearest `seconds`, before zero or not, while a double holds it
// exactly. Validate before casting: finite times can still overflow when
// multiplied by the rate.
inline std::optional<std::int64_t> nearestSample(double seconds, double sampleRate) {
    constexpr double largestExactIndex = 9007199254740991.0;
    const auto value = std::round(seconds * sampleRate);
    if (!std::isfinite(value) || std::abs(value) > largestExactIndex) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

// Live playback and offline export use the same integer clock from zero.
inline std::optional<std::int64_t> sampleIndex(double seconds, double sampleRate) {
    if (!std::isfinite(seconds) || seconds < 0.0 || !std::isfinite(sampleRate) || sampleRate <= 0.0) {
        return std::nullopt;
    }
    return nearestSample(seconds, sampleRate);
}
}
