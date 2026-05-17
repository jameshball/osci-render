#pragma once

namespace osci {

inline constexpr float kOutputClipBypassThreshold = 1.0f - 1.0e-5f;
inline constexpr float kOutputClipPeakEpsilon = 1.0e-5f;

inline bool isOutputClipActive(float threshold) noexcept {
    return threshold < kOutputClipBypassThreshold;
}

inline float limitSymmetric(float value, float threshold) noexcept {
    const float positiveThreshold = threshold > 0.0f ? threshold : 0.0f;

    if (value > positiveThreshold) {
        return positiveThreshold;
    }

    if (value < -positiveThreshold) {
        return -positiveThreshold;
    }

    return value;
}

inline float applyVolumeAndOptionalClip(float sample, float volume, float threshold) noexcept {
    const float scaled = sample * volume;

    if (isOutputClipActive(threshold)) {
        return limitSymmetric(scaled, threshold);
    }

    return scaled;
}

} // namespace osci
