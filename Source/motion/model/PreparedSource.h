#pragma once

#include "../../audio/synth/PreparedDrawing.h"
#include "PreparedPointFrames.h"
#include <memory>

namespace motion {
// Fully prepared immutable geometry. Construction and destruction happen off
// the render thread; sampling only reads shared drawings and fixed frame data.
class PreparedSource {
public:
    PreparedSource(std::vector<std::shared_ptr<const osci::PreparedDrawing>> frames, double framesPerSecond)
        : frames(std::move(frames)), framesPerSecond(framesPerSecond) {}

    explicit PreparedSource(std::shared_ptr<const PreparedPointFrames> points)
        : framesPerSecond(points != nullptr ? points->frameRate() : 0), points(std::move(points)) {}

    std::size_t frameCount() const { return points != nullptr ? points->frameCount() : frames.size(); }
    double frameRate() const { return framesPerSecond; }
    bool hasExplicitColour() const { return points != nullptr && points->hasExplicitColour(); }
    double duration() const {
        const auto seconds = static_cast<double>(frameCount()) / framesPerSecond;
        return std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
    }

    std::size_t frameIndex(double localSeconds) const {
        if (points != nullptr) { return points->frameIndex(localSeconds); }
        if (frames.size() <= 1 || !std::isfinite(localSeconds) || !std::isfinite(framesPerSecond) || framesPerSecond <= 0.0) {
            return 0;
        }
        const auto seconds = duration();
        if (seconds <= 0.0) {
            return 0;
        }
        auto wrapped = std::fmod(localSeconds, seconds);
        if (wrapped < 0.0) {
            wrapped += seconds;
        }
        const auto index = std::floor(wrapped * framesPerSecond);
        if (!std::isfinite(index) || index < 0.0) {
            return 0;
        }
        return index >= static_cast<double>(frames.size()) ? frames.size() - 1 : static_cast<std::size_t>(index);
    }

    std::shared_ptr<const osci::PreparedDrawing> firstFrame() const { return frames.empty() ? nullptr : frames.front(); }

    osci::Point sample(double localSeconds, double phase) const {
        if (points != nullptr) {
            const auto point = points->sample(localSeconds, phase);
            return {point.x, point.y, point.z, point.r, point.g, point.b};
        }
        if (frames.empty() || !std::isfinite(localSeconds)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        const auto& frame = frames[frameIndex(localSeconds)];
        return frame != nullptr ? frame->sample(phase) : osci::Point(0, 0, 0, 0, 0, 0);
    }

private:
    const std::vector<std::shared_ptr<const osci::PreparedDrawing>> frames;
    const double framesPerSecond;
    const std::shared_ptr<const PreparedPointFrames> points;
};
}
