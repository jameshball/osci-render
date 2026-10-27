#pragma once

#include <algorithm>
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

// One sample of playback at `speed`: the playhead moves on by the speed (a
// fraction carried in `carry` to the next sample), and crossing the loop's
// end goes back to its start, as reaching the composition's end goes to 0.
// A playhead that started past the loop plays on to the end. True when it
// jumped, which the beam guards against.
struct PlayheadStep {
    std::int64_t sample = 0;
    bool jumped = false;
};
inline PlayheadStep advancePlayhead(std::int64_t sample, double speed, double& carry, std::int64_t loopStart, std::int64_t loopEnd, std::int64_t duration) {
    auto next = sample;
    if (speed == 1.0) {
        ++next;
    } else {
        carry += speed;
        const auto steps = static_cast<std::int64_t>(carry);
        carry -= static_cast<double>(steps);
        next += steps;
    }
    // Backwards, the same the other way: the loop's start goes back to its
    // end, and the composition's start to its end.
    if (speed < 0) {
        if (loopEnd > loopStart && sample >= loopStart && next < loopStart) { return {loopEnd - 1, true}; }
        if (next < 0) { return {std::max<std::int64_t>(0, duration - 1), true}; }
        return {next, false};
    }
    if (loopEnd > loopStart && sample < loopEnd && next >= loopEnd) { return {loopStart, true}; }
    if (next >= duration) { return {0, true}; }
    return {next, false};
}
}
