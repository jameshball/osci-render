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
        bool explicitColour = false, anyDark = false;
        for (const auto& point : points) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
                || !validColour(point)) { return { nullptr, "Point samples require finite XYZ and RGB in [0,1], or RGB all -1." }; }
            explicitColour = explicitColour || point.r != -1;
            anyDark = anyDark || dark(point);
        }
        try {
            // Allocate shared ownership before moving the caller's vector so an
            // allocation failure leaves that vector intact.
            auto result = std::shared_ptr<PreparedPointFrames>(new PreparedPointFrames(frameRate, static_cast<std::size_t>(frames), pointsPerFrame, explicitColour));
            // One global prefix count permits independent per-frame range
            // queries without per-frame allocations. Only needed for blanking
            // guards: at most 42.67 MiB + 4 bytes beyond the 256 MiB raw payload.
            // Build before moving input so allocation failure retains ownership.
            if (anyDark) {
                result->darkPrefix.resize(points.size() + 1);
                for (std::size_t i = 0; i < points.size(); ++i) {
                    result->darkPrefix[i + 1] = result->darkPrefix[i] + (dark(points[i]) ? 1u : 0u);
                }
            }
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
    // phaseSpan is the caller's phase travel per output sample. Expanding dark
    // guards in both directions prevents decimation from skipping travel blanks.
    // Zero retains legacy interpolation; no temporal sampling state is stored.
    PointSample sample(double seconds, double phase, double phaseSpan = 0) const {
        if (!std::isfinite(seconds) || !std::isfinite(phase)) { return {}; }
        return sampleFrame(frameIndex(seconds), phase, phaseSpan);
    }

    PointSample sampleFrame(std::size_t frame, double phase, double phaseSpan = 0) const {
        if (frame >= frames || !std::isfinite(phase)) { return {}; }
        const auto position = std::clamp(phase, 0.0, std::nextafter(1.0, 0.0)) * static_cast<double>(stride);
        const auto first = std::min(stride - 1, static_cast<std::size_t>(position));
        const auto next = (first + 1) % stride;
        const auto fraction = std::clamp(position - static_cast<double>(first), 0.0, 1.0);
        const auto offset = frame * stride;
        const auto& a = points[offset + first];
        const auto& b = points[offset + next];

        const auto interpolate = [fraction](float a, float b) {
            return static_cast<float>(static_cast<double>(a) * (1 - fraction) + static_cast<double>(b) * fraction);
        };
        const bool sentinel = a.r == -1 || b.r == -1;
        auto result = fraction == 0 ? a : PointSample { interpolate(a.x, b.x), interpolate(a.y, b.y), interpolate(a.z, b.z),
            sentinel ? -1 : interpolate(a.r, b.r), sentinel ? -1 : interpolate(a.g, b.g), sentinel ? -1 : interpolate(a.b, b.b) };
        if (!std::isfinite(phaseSpan) || phaseSpan < 0
            || (phaseSpan > 0 && hasDarkInSpan(frame, position, phaseSpan))) {
            result.r = result.g = result.b = 0;
        }
        return result;
    }

private:
    PreparedPointFrames(double rate, std::size_t frames, std::size_t stride, bool explicitColour) : rate(rate), frames(frames), stride(stride), explicitColour(explicitColour) {}
    static bool validColour(const PointSample& point) {
        if (point.r == -1 && point.g == -1 && point.b == -1) { return true; }
        const auto valid = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
        return valid(point.r) && valid(point.g) && valid(point.b);
    }
    static bool dark(const PointSample& point) { return point.r == 0 && point.g == 0 && point.b == 0; }
    bool hasDarkInSpan(std::size_t frame, double position, double phaseSpan) const {
        if (darkPrefix.empty()) { return false; }
        const auto offset = frame * stride;
        if (darkPrefix[offset + stride] == darkPrefix[offset]) { return false; }
        if (phaseSpan >= 0.5) { return true; }
        // Values are bounded by [-stride/2, 1.5*stride] before conversion.
        // Floor/ceil conservatively include both interpolation support points.
        const auto radius = phaseSpan * static_cast<double>(stride);
        const auto low = static_cast<std::ptrdiff_t>(std::floor(position - radius));
        const auto high = static_cast<std::ptrdiff_t>(std::ceil(position + radius));
        const auto count = static_cast<std::size_t>(high - low + 1);
        if (count >= stride) { return true; }
        const auto start = static_cast<std::size_t>((low + static_cast<std::ptrdiff_t>(stride)) % static_cast<std::ptrdiff_t>(stride));
        const auto end = start + count;
        if (end <= stride) { return darkPrefix[offset + end] != darkPrefix[offset + start]; }
        return darkPrefix[offset + stride] != darkPrefix[offset + start]
            || darkPrefix[offset + end - stride] != darkPrefix[offset];
    }
    const double rate;
    const std::size_t frames, stride;
    const bool explicitColour;
    std::vector<PointSample> points;
    std::vector<std::uint32_t> darkPrefix;
};
}
