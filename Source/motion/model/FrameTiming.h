#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace motion {
// A source's time looped into [0, length): negative times wrap into its end.
inline double wrapTime(double seconds, double length) {
    const auto wrapped = std::fmod(seconds, length);
    return wrapped < 0 ? wrapped + length : wrapped;
}

// Immutable presentation timing for sources with unequal frame durations.
// GIF delays retain their authored millisecond boundaries; live captures may
// provide more precise cumulative-second boundaries without quantisation.
class FrameTiming {
public:
    struct Result {
        std::shared_ptr<const FrameTiming> timing;
        std::string error;
        explicit operator bool() const { return timing != nullptr; }
    };
    static Result create(const std::vector<std::uint32_t>& milliseconds) {
        if (milliseconds.empty() || milliseconds.size() > 100000) { return {nullptr, "Frame timing requires 1-100000 frames."}; }
        std::uint64_t total = 0;
        for (const auto delay : milliseconds) {
            if (delay == 0 || delay > 3600000) { return {nullptr, "Frame durations must be between 1 millisecond and one hour."}; }
            total += delay;
            if (total > 86400000) { return {nullptr, "Animated source duration exceeds 24 hours."}; }
        }
        std::vector<double> ends;
        ends.reserve(milliseconds.size());
        std::uint64_t end = 0;
        for (const auto delay : milliseconds) {
            end += delay;
            ends.push_back(static_cast<double>(end) / 1000.0);
        }
        return createFromEndSeconds(std::move(ends));
    }
    static Result createFromEndSeconds(std::vector<double> ends) {
        if (ends.empty() || ends.size() > 100000) { return {nullptr, "Frame timing requires 1-100000 frames."}; }
        double previous = 0;
        for (const auto end : ends) {
            if (!std::isfinite(end) || end <= previous || end > 86400.0) {
                return {nullptr, "Frame end times must be finite, strictly increasing and no later than 24 hours."};
            }
            previous = end;
        }
        auto timing = std::shared_ptr<FrameTiming>(new FrameTiming());
        timing->ends = std::move(ends);
        return {std::move(timing), {}};
    }
    std::size_t frameCount() const { return ends.size(); }
    double duration() const { return ends.back(); }
    double frameStart(std::size_t index) const { return index == 0 ? 0 : ends.at(index - 1); }
    double frameEnd(std::size_t index) const { return ends.at(index); }
    double averageFrameRate() const { return static_cast<double>(ends.size()) / duration(); }
    std::size_t frameIndex(double seconds) const {
        if (!std::isfinite(seconds)) { return 0; }
        const auto found = std::upper_bound(ends.begin(), ends.end(), wrapTime(seconds, duration()));
        return std::min(static_cast<std::size_t>(found - ends.begin()), ends.size() - 1);
    }
private:
    FrameTiming() = default;
    std::vector<double> ends;
};
}
