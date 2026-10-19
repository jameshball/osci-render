#pragma once

#include "BlenderFrame.h"
#include "CaptureEnding.h"
#include <memory>

namespace motion {
// Receiver-owned, bounded history. Allocate before acquiring the receiver lock;
// append only retains immutable frames in the already reserved array. Failed
// captures retain their storage until the owner moves it out for destruction.
// A capture that reaches a limit, or whose Blender disconnects, ends there
// and keeps what it has.
class BlenderCapture {
public:
    struct Frame {
        std::shared_ptr<const BlenderFrame> geometry;
        double start = 0;
    };
    enum class Failure { none, clock };
    static constexpr std::size_t maximumFrames = 20000;
    static constexpr std::size_t maximumSegments = 1000000;
    static constexpr std::size_t maximumRetainedBytes = 64 * 1024 * 1024;
    static constexpr double maximumSeconds = 600;

    explicit BlenderCapture(bool freeze) : freezeOnDisconnect(freeze) { frames.reserve(maximumFrames); }
    void begin(double now, std::shared_ptr<const BlenderFrame> initial) {
        origin = now;
        append(now, std::move(initial));
    }
    void append(double now, std::shared_ptr<const BlenderFrame> geometry) {
        if (!checkTime(now)) { return; }
        if (ending != CaptureEnding::none) { return; }
        // Repeated blank events do not consume the update budget. Exact-time
        // collisions are retained until finalisation; zero-length states vanish.
        if (!frames.empty() && frames.back().geometry == geometry) { return; }
        const auto count = geometry != nullptr ? geometry->segments.size() : 0;
        const auto bytes = geometry != nullptr ? geometry->segments.capacity() * sizeof(BlenderSegment) + sizeof(BlenderFrame) : 0;
        if (frames.size() >= maximumFrames) { end(now, CaptureEnding::updates); return; }
        if (count > maximumSegments - segments || bytes > maximumRetainedBytes - retainedBytes) {
            end(now, CaptureEnding::size);
            return;
        }
        frames.push_back({std::move(geometry), now - origin});
        segments += count;
        retainedBytes += bytes;
    }
    bool checkTime(double now) {
        if (failure != Failure::none) { return false; }
        if (!std::isfinite(now) || now < origin || (!frames.empty() && now - origin < frames.back().start)) {
            failure = Failure::clock;
            return false;
        }
        if (now - origin > maximumSeconds) { end(origin + maximumSeconds, CaptureEnding::duration); }
        return true;
    }
    // Stops recording at `now`, keeping what came before.
    void end(double now, CaptureEnding why) {
        if (failure != Failure::none || ending != CaptureEnding::none) { return; }
        ending = why;
        duration = std::min(now - origin, maximumSeconds);
        if (!(duration > 0)) { failure = Failure::clock; }
    }
    void finish(double now) {
        if (checkTime(now) && ending == CaptureEnding::none) {
            duration = now - origin;
            if (!(duration > 0)) { failure = Failure::clock; }
        }
    }
    bool ended() const { return ending != CaptureEnding::none; }
    const char* error() const { return failure == Failure::clock ? "Capture timing is invalid. Please record again." : ""; }
    const bool freezeOnDisconnect;
    std::vector<Frame> frames;
    double duration = 0;
    Failure failure = Failure::none;
    CaptureEnding ending = CaptureEnding::none;
private:
    double origin = 0;
    std::size_t segments = 0, retainedBytes = 0;
};
}
