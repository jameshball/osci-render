#pragma once

#include "BlenderFrame.h"
#include <memory>

namespace motion {
// Receiver-owned, bounded history. Allocate before acquiring the receiver lock;
// append only retains immutable frames in the already reserved array. Failed
// captures retain their storage until the owner moves it out for destruction.
class BlenderCapture {
public:
    struct Frame {
        std::shared_ptr<const BlenderFrame> geometry;
        double start = 0;
    };
    enum class Failure { none, clock, duration, frames, geometry };
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
        // Repeated blank events do not consume the update budget. Exact-time
        // collisions are retained until finalisation; zero-length states vanish.
        if (!frames.empty() && frames.back().geometry == geometry) { return; }
        const auto count = geometry != nullptr ? geometry->segments.size() : 0;
        const auto bytes = geometry != nullptr ? geometry->segments.capacity() * sizeof(BlenderSegment) + sizeof(BlenderFrame) : 0;
        if (frames.size() >= maximumFrames) { failure = Failure::frames; return; }
        if (count > maximumSegments - segments || bytes > maximumRetainedBytes - retainedBytes) {
            failure = Failure::geometry;
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
        if (now - origin > maximumSeconds) { failure = Failure::duration; return false; }
        return true;
    }
    void finish(double now) {
        if (checkTime(now)) {
            duration = now - origin;
            if (!(duration > 0)) { failure = Failure::clock; }
        }
    }
    const char* error() const {
        switch (failure) {
            case Failure::none: return "";
            case Failure::clock: return "Capture timing is invalid. Please record again.";
            case Failure::duration: return "Capture exceeded the ten-minute limit. No partial capture was saved.";
            case Failure::frames: return "Capture exceeded 20,000 updates. No partial capture was saved.";
            case Failure::geometry: return "Capture exceeded its geometry budget. No partial capture was saved.";
        }
        return "Capture failed.";
    }
    const bool freezeOnDisconnect;
    std::vector<Frame> frames;
    double duration = 0;
    Failure failure = Failure::none;
private:
    double origin = 0;
    std::size_t segments = 0, retainedBytes = 0;
};
}
