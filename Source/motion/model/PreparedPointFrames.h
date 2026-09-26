#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace motion {
struct PointSample {
    float x = 0, y = 0, z = 0, r = 0, g = 0, b = 0;
};
static_assert(sizeof(PointSample) == 6 * sizeof(float));

// Immutable, uniformly sampled frame geometry. Prepare and destroy off the
// realtime thread. Sampling does not allocate, lock, or reparameterize by length.
class PreparedPointFrames {
public:
    static constexpr std::size_t maximumBytes = 256 * 1024 * 1024;
    static constexpr std::uint64_t maximumFrames = 100000;
    static constexpr std::size_t minimumPointsPerFrame = 16, maximumPointsPerFrame = 16384;
    static constexpr double maximumFrameRate = 240;
    struct Result {
        std::shared_ptr<const PreparedPointFrames> source;
        std::string error;
        explicit operator bool() const { return source != nullptr; }
    };

    // Builders must call this BEFORE allocating frames*pointsPerFrame entries.
    // Success guarantees that multiplication fits size_t and the payload budget.
    static std::string validate(double frameRate, std::uint64_t frames, std::size_t pointsPerFrame) {
        if (!std::isfinite(frameRate) || frameRate <= 0 || frameRate > maximumFrameRate) {
            return "Point frame rate must be finite, positive and at most 240 FPS.";
        }
        if (frames == 0 || frames > maximumFrames) { return "Point sources require 1-100000 frames."; }
        if (pointsPerFrame < minimumPointsPerFrame || pointsPerFrame > maximumPointsPerFrame) {
            return "Each point frame must contain 16-16384 samples.";
        }
        if (frames > (maximumBytes / sizeof(PointSample)) / pointsPerFrame) {
            return "Decoded point frames exceed the 256 MiB payload limit.";
        }
        const auto seconds = static_cast<double>(frames) / frameRate;
        if (!std::isfinite(seconds) || seconds <= 0) { return "Point source duration must be finite and positive."; }
        return {};
    }

    // Takes ownership only on success. Invalid metadata/content never exposes
    // a partial source. Excess reserved capacity also counts toward the budget.
    static Result create(double frameRate, std::uint64_t frames, std::size_t pointsPerFrame, std::vector<PointSample>&& points) {
        const auto error = validate(frameRate, frames, pointsPerFrame);
        if (!error.empty()) { return { nullptr, error }; }
        if (points.size() != static_cast<std::size_t>(frames) * pointsPerFrame) {
            return { nullptr, "Point sample count must exactly match frames times samples per frame." };
        }
        if (points.capacity() > maximumBytes / sizeof(PointSample)) {
            return { nullptr, "Reserved point storage exceeds the 256 MiB payload limit." };
        }
        bool explicitColour = false;
        for (const auto& point : points) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
                || !validColour(point)) { return { nullptr, "Point samples require finite XYZ and RGB in [0,1], or RGB all -1." }; }
            explicitColour = explicitColour || point.r != -1;
        }
        try {
            // Allocate shared ownership before moving the caller's vector so an
            // allocation failure leaves that vector intact.
            auto result = std::shared_ptr<PreparedPointFrames>(new PreparedPointFrames(frameRate, static_cast<std::size_t>(frames), pointsPerFrame, explicitColour));
            result->points = std::move(points);
            return { std::move(result), {} };
        } catch (const std::bad_alloc&) {
            return { nullptr, "Not enough memory to retain prepared point frames." };
        }
    }

    bool hasExplicitColour() const { return explicitColour; }
    std::size_t frameCount() const { return frames; }
    double frameRate() const { return rate; }
    double duration() const { return static_cast<double>(frames) / rate; }
    std::size_t pointsPerFrame() const { return stride; }
    const std::vector<PointSample>& data() const { return points; }

    // Negative local times wrap into the final source frame, matching PreparedSource.
    std::size_t frameIndex(double seconds) const {
        if (frames == 1 || !std::isfinite(seconds)) { return 0; }
        auto wrapped = std::fmod(seconds, duration());
        if (wrapped < 0) { wrapped += duration(); }
        const auto index = std::floor(wrapped * rate);
        return index >= static_cast<double>(frames) ? frames - 1 : static_cast<std::size_t>(index);
    }

    // Phase clamps to [0,nextafter(1,0)] rather than wrapping. Interpolate the
    // final sample toward sample zero within this frame, never into another frame.
    // Sentinel colour remains sentinel across a mixed sentinel/explicit segment;
    // exact point samples retain their own colour. Invalid runtime inputs are dark.
    PointSample sample(double seconds, double phase) const {
        if (!std::isfinite(seconds) || !std::isfinite(phase)) { return {}; }
        const auto position = std::clamp(phase, 0.0, std::nextafter(1.0, 0.0)) * static_cast<double>(stride);
        const auto first = std::min(stride - 1, static_cast<std::size_t>(position));
        const auto next = (first + 1) % stride;
        const auto fraction = std::clamp(position - static_cast<double>(first), 0.0, 1.0);
        const auto offset = frameIndex(seconds) * stride;
        const auto& a = points[offset + first];
        const auto& b = points[offset + next];
        if (fraction == 0) { return a; }
        const auto interpolate = [fraction](float a, float b) {
            return static_cast<float>(static_cast<double>(a) * (1 - fraction) + static_cast<double>(b) * fraction);
        };
        const bool sentinel = a.r == -1 || b.r == -1;
        return { interpolate(a.x, b.x), interpolate(a.y, b.y), interpolate(a.z, b.z),
            sentinel ? -1 : interpolate(a.r, b.r), sentinel ? -1 : interpolate(a.g, b.g), sentinel ? -1 : interpolate(a.b, b.b) };
    }

private:
    PreparedPointFrames(double rate, std::size_t frames, std::size_t stride, bool explicitColour) : rate(rate), frames(frames), stride(stride), explicitColour(explicitColour) {}
    static bool validColour(const PointSample& point) {
        if (point.r == -1 && point.g == -1 && point.b == -1) { return true; }
        const auto valid = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
        return valid(point.r) && valid(point.g) && valid(point.b);
    }
    const double rate;
    const std::size_t frames, stride;
    const bool explicitColour;
    std::vector<PointSample> points;
};
}
