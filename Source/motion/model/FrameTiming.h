#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace motion {
// Immutable presentation timing for sources with unequal frame durations.
// Millisecond boundaries keep GIF centisecond delays exact during preparation.
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
        auto timing = std::shared_ptr<FrameTiming>(new FrameTiming());
        timing->ends.reserve(milliseconds.size());
        std::uint64_t end = 0;
        for (const auto delay : milliseconds) {
            end += delay;
            timing->ends.push_back(end);
        }
        return {std::move(timing), {}};
    }
    std::size_t frameCount() const { return ends.size(); }
    double duration() const { return static_cast<double>(ends.back()) / 1000; }
    double averageFrameRate() const { return static_cast<double>(ends.size()) / duration(); }
    std::size_t frameIndex(double seconds) const {
        if (!std::isfinite(seconds)) { return 0; }
        auto wrapped = std::fmod(seconds, duration());
        if (wrapped < 0) { wrapped += duration(); }
        const auto found = std::upper_bound(ends.begin(), ends.end(), wrapped,
            [](double value, std::uint64_t end) { return value < static_cast<double>(end) / 1000; });
        return std::min(static_cast<std::size_t>(found - ends.begin()), ends.size() - 1);
    }
private:
    FrameTiming() = default;
    std::vector<std::uint64_t> ends;
};
}
