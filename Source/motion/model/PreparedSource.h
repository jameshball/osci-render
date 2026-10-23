#pragma once

#include "AssetKinds.h"
#include "PreparedDrawing.h"
#include "PreparedPointFrames.h"
#include "FrameTiming.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace motion {
// Fully prepared immutable geometry. Construction and destruction happen off
// the render thread; sampling only reads shared drawings and fixed frame data.
class PreparedSource {
public:
    PreparedSource(std::vector<std::shared_ptr<const motion::PreparedDrawing>> frames, double framesPerSecond)
        : frames(std::move(frames)), framesPerSecond(framesPerSecond), faced(anyFaces(this->frames)) {}

    explicit PreparedSource(std::shared_ptr<const PreparedPointFrames> points)
        : framesPerSecond(points != nullptr ? points->frameRate() : 0), points(std::move(points)) {}

    PreparedSource(std::shared_ptr<const PreparedPointFrames> points, std::shared_ptr<const FrameTiming> timing)
        : framesPerSecond(timing != nullptr ? timing->averageFrameRate() : 0), points(std::move(points)), timing(std::move(timing)) {
        if (this->points == nullptr || this->timing == nullptr || this->points->frameCount() != this->timing->frameCount()) {
            throw std::invalid_argument("Prepared source frame data and timing must have matching frame counts.");
        }
    }

    PreparedSource(std::vector<std::shared_ptr<const motion::PreparedDrawing>> frameDrawings, std::shared_ptr<const FrameTiming> frameTiming)
        : frames(std::move(frameDrawings)), framesPerSecond(frameTiming != nullptr ? frameTiming->averageFrameRate() : 0), timing(std::move(frameTiming)), faced(anyFaces(frames)) {
        if (frames.empty() || timing == nullptr || frames.size() != timing->frameCount()) {
            throw std::invalid_argument("Prepared drawing frames and timing must have matching frame counts.");
        }
    }

    std::size_t frameCount() const { return points != nullptr ? points->frameCount() : frames.size(); }
    // Whether any of its drawings has faces (a mesh, or a filled shape): a
    // surface for lines to be laid on and to reflect with.
    bool hasFaces() const { return faced; }
    double frameRate() const { return framesPerSecond; }
    std::size_t previewSampleCount() const { return points != nullptr ? points->pointsPerFrame() : 512; }
    double previewPhaseSpan() const { return points != nullptr ? 0.0 : 1.0 / 512; }
    bool hasExplicitColour() const { return points != nullptr && points->hasExplicitColour(); }
    // The raw frames of a point source; null for vector sources.
    const PreparedPointFrames* pointFrames() const { return points.get(); }
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
        const auto index = std::floor(wrapTime(localSeconds, seconds) * framesPerSecond);
        if (!std::isfinite(index) || index < 0.0) {
            return 0;
        }
        return index >= static_cast<double>(frames.size()) ? frames.size() - 1 : static_cast<std::size_t>(index);
    }

    std::shared_ptr<const motion::PreparedDrawing> firstFrame() const { return frames.empty() ? nullptr : frames.front(); }
    // Vector frame for exact traversal; null for point-frame sources.
    const motion::PreparedDrawing* drawingAt(std::size_t frame) const { return frame < frames.size() ? frames[frame].get() : nullptr; }

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
        const auto wrapped = wrapTime(seconds, length);
        const auto index = frameIndex(seconds);
        const auto start = timing != nullptr ? timing->frameStart(index) : static_cast<double>(index) / framesPerSecond;
        const auto end = timing != nullptr ? timing->frameEnd(index) : static_cast<double>(index + 1) / framesPerSecond;
        return wrapped - span <= start || wrapped + span >= end;
    }
    const std::vector<std::shared_ptr<const motion::PreparedDrawing>> frames;
    const double framesPerSecond;
    const std::shared_ptr<const PreparedPointFrames> points;
    const std::shared_ptr<const FrameTiming> timing;
    const bool faced = false;

    static bool anyFaces(const std::vector<std::shared_ptr<const motion::PreparedDrawing>>& drawings) {
        return std::any_of(drawings.begin(), drawings.end(), [](const auto& drawing) { return drawing != nullptr && !drawing->faces().empty(); });
    }
};

// The frames a clip's own source samples its content with, from its start to
// `contentEnd`: one a frame at `frameRate`, or for content longer than
// maximumSourceFrames frames, that many spread over it, so they reach its
// end rather than loop back to its start.
struct ContentFrames {
    double rate;
    std::size_t count;
};
inline ContentFrames contentFrames(double contentEnd, double frameRate) {
    constexpr auto maximum = maximumSourceFrames;
    const auto base = std::isfinite(frameRate) && frameRate > 0 ? frameRate : 60.0;
    const auto span = std::isfinite(contentEnd) && contentEnd > 0 ? contentEnd : 0.0;
    // The last frame falls on the content's end, inside the frames' duration.
    if (span * base >= static_cast<double>(maximum)) { return {static_cast<double>(maximum - 1) / span, maximum}; }
    return {base, static_cast<std::size_t>(std::floor(span * base)) + 1};
}

// A clip's source sampled a frame per frame of its content (contentFrames):
// `valueAt(seconds)` is what draws it then and `drawingOf(value)` its
// drawing, or null if it can't be drawn (and then there is no source).
// Frames whose values match share a drawing, and a clip whose value never
// changes has a single frame.
template <typename ValueAt, typename DrawingOf>
std::shared_ptr<const PreparedSource> sampledSource(double contentEnd, double frameRate, ValueAt&& valueAt, DrawingOf&& drawingOf) {
    const auto [rate, count] = contentFrames(contentEnd, frameRate);
    std::vector<std::shared_ptr<const PreparedDrawing>> frames;
    frames.reserve(count);
    std::decay_t<decltype(valueAt(0.0))> previous {};
    bool varies = false;
    for (std::size_t frame = 0; frame < count; ++frame) {
        auto value = valueAt(static_cast<double>(frame) / rate);
        if (frame > 0 && value == previous) {
            frames.push_back(frames.back());
            continue;
        }
        auto drawing = drawingOf(value);
        if (drawing == nullptr) { return nullptr; }
        varies = varies || frame > 0;
        frames.push_back(std::move(drawing));
        previous = std::move(value);
    }
    if (!varies) { frames.resize(1); }
    return std::make_shared<PreparedSource>(std::move(frames), rate);
}
}
