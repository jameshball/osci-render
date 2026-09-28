#pragma once

#include "../../audio/synth/PreparedDrawing.h"
#include "PreparedPointFrames.h"
#include "FrameTiming.h"
#include <memory>
#include <stdexcept>
#include <utility>

namespace motion {
// Fully prepared immutable geometry. Construction and destruction happen off
// the render thread; sampling only reads shared drawings and fixed frame data.
class PreparedSource {
public:
    PreparedSource(std::vector<std::shared_ptr<const osci::PreparedDrawing>> frames, double framesPerSecond)
        : frames(std::move(frames)), framesPerSecond(framesPerSecond) {}

    explicit PreparedSource(std::shared_ptr<const PreparedPointFrames> points)
        : framesPerSecond(points != nullptr ? points->frameRate() : 0), points(std::move(points)) {}

    PreparedSource(std::shared_ptr<const PreparedPointFrames> points, std::shared_ptr<const FrameTiming> timing)
        : framesPerSecond(timing != nullptr ? timing->averageFrameRate() : 0), points(std::move(points)), timing(std::move(timing)) {
        if (this->points == nullptr || this->timing == nullptr || this->points->frameCount() != this->timing->frameCount()) {
            throw std::invalid_argument("Prepared source frame data and timing must have matching frame counts.");
        }
    }

    PreparedSource(std::vector<std::shared_ptr<const osci::PreparedDrawing>> frameDrawings, std::shared_ptr<const FrameTiming> frameTiming)
        : frames(std::move(frameDrawings)), framesPerSecond(frameTiming != nullptr ? frameTiming->averageFrameRate() : 0), timing(std::move(frameTiming)) {
        if (frames.empty() || timing == nullptr || frames.size() != timing->frameCount()) {
            throw std::invalid_argument("Prepared drawing frames and timing must have matching frame counts.");
        }
    }

    std::size_t frameCount() const { return points != nullptr ? points->frameCount() : frames.size(); }
    double frameRate() const { return framesPerSecond; }
    std::size_t previewSampleCount() const { return points != nullptr ? points->pointsPerFrame() : 512; }
    double previewPhaseSpan() const { return points != nullptr ? 0.0 : 1.0 / 512; }
    bool hasExplicitColour() const { return points != nullptr && points->hasExplicitColour(); }
    double duration() const {
        if (timing != nullptr) { return timing->duration(); }
        const auto seconds = static_cast<double>(frameCount()) / framesPerSecond;
        return std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
    }

    std::size_t frameIndex(double localSeconds) const {
        if (timing != nullptr) { return timing->frameIndex(localSeconds); }
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
    // Vector frame for exact traversal; null for point-frame sources.
    const osci::PreparedDrawing* drawingAt(std::size_t frame) const { return frame < frames.size() ? frames[frame].get() : nullptr; }

    // Samples one latched frame. phaseSpan blanks drawing discontinuities and
    // point-source travel exactly as sample() does, without frame guards.
    osci::Point sampleFrame(std::size_t frame, double phase, double phaseSpan = 0) const {
        if (points != nullptr) {
            const auto point = points->sampleFrame(frame, phase, phaseSpan);
            return {point.x, point.y, point.z, point.r, point.g, point.b};
        }
        const auto* drawing = drawingAt(frame);
        return drawing != nullptr ? drawing->sample(phase, phaseSpan) : osci::Point(0, 0, 0, 0, 0, 0);
    }

    osci::Point sample(double localSeconds, double phase, double phaseSpan = 0, double timeSpan = 0) const {
        if (points != nullptr) {
            if (!std::isfinite(localSeconds)) { return {0, 0, 0, 0, 0, 0}; }
            const auto point = points->sampleFrame(frameIndex(localSeconds), phase, phaseSpan);
            const bool blank = crossesFrameBoundary(localSeconds, timeSpan);
            return {point.x, point.y, point.z, blank ? 0 : point.r, blank ? 0 : point.g, blank ? 0 : point.b};
        }
        if (frames.empty() || !std::isfinite(localSeconds)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        const auto& frame = frames[frameIndex(localSeconds)];
        auto point = frame != nullptr ? frame->sample(phase, phaseSpan) : osci::Point(0, 0, 0, 0, 0, 0);
        if (crossesFrameBoundary(localSeconds, timeSpan)) { point.r = point.g = point.b = 0; }
        return point;
    }

private:
    bool crossesFrameBoundary(double seconds, double span) const {
        if (!std::isfinite(span) || span < 0) { return true; }
        if (span == 0 || frameCount() <= 1) { return false; }
        const auto length = duration();
        if (!(length > 0) || span >= length) { return true; }
        auto wrapped = std::fmod(seconds, length);
        if (wrapped < 0) { wrapped += length; }
        const auto index = frameIndex(seconds);
        const auto start = timing != nullptr ? timing->frameStart(index) : static_cast<double>(index) / framesPerSecond;
        const auto end = timing != nullptr ? timing->frameEnd(index) : static_cast<double>(index + 1) / framesPerSecond;
        return wrapped - span <= start || wrapped + span >= end;
    }
    const std::vector<std::shared_ptr<const osci::PreparedDrawing>> frames;
    const double framesPerSecond;
    const std::shared_ptr<const PreparedPointFrames> points;
    const std::shared_ptr<const FrameTiming> timing;
};
}
