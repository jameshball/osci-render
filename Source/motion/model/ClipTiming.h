#pragma once

#include "Tempo.h"
#include <cmath>
#include <optional>

namespace motion {
struct ClipTiming {
    ClipTiming(double first = 0, double last = 0, double sourceOffset = 0, double speed = 1)
        : start(first), offset(sourceOffset), rate(speed), finish(last), length(last - first) {}
    // A musical clip under a tempo map advances its content with beats, not
    // seconds: content = offset + secondsPerBeat * (beat(time) - startBeat),
    // where beat(time) reads the map at an affine input time (identity for a
    // top-level clip; nesting composes the affine part). `rate` stays the
    // average speed, which agrees with the warp at both ends.
    struct BeatWarp {
        Tempo tempo;
        double inputOffset = 0, inputScale = 1, startBeat = 0, secondsPerBeat = 1;
        double beat(double time) const { return tempo.beats(inputOffset + inputScale * time); }
        double time(double beat) const { return (tempo.seconds(beat) - inputOffset) / inputScale; }
    };
    double start, offset, rate;
    std::optional<BeatWarp> warp;
    double end() const { return finish; }
    double duration() const { return length; }
    void moveTo(double value) { start = value; finish = value + length; }
    void setStart(double value) { start = value; length = finish - start; }
    void setEnd(double value) { finish = value; length = finish - start; }
    void setDuration(double value) { length = value; finish = start + value; }
    // Content time at a scope time, and its inverse. Every conversion between
    // clip content and its scope goes through these two.
    double localTime(double time) const {
        return warp.has_value() ? offset + warp->secondsPerBeat * (warp->beat(time) - warp->startBeat) : offset + (time - start) * rate;
    }
    double projectTime(double local) const {
        return warp.has_value() ? warp->time(warp->startBeat + (local - offset) / warp->secondsPerBeat) : start + (local - offset) / rate;
    }
    // Resolve this child interval through a composition instance. Both inputs
    // are seconds at their own scope; authored curves remain in source time.
    // Clipping the visible interval also advances the source offset, so a trim
    // cannot restart the child's animation.
    std::optional<ClipTiming> nestedIn(const ClipTiming& instance) const {
        if (!valid() || !instance.valid()) { return std::nullopt; }
        const auto mappedStart = instance.projectTime(start);
        const auto mappedEnd = instance.projectTime(end());
        if (!std::isfinite(mappedStart) || !std::isfinite(mappedEnd)) { return std::nullopt; }
        const auto first = std::max(instance.start, mappedStart);
        const auto last = std::min(instance.end(), mappedEnd);
        if (first >= last) { return std::nullopt; }
        ClipTiming resolved(first, last, localTime(instance.localTime(first)), rate * instance.rate);
        if (instance.warp.has_value()) {
            // Two tempo warps cannot compose exactly: inside a musical
            // instance, a child plays at its own average speed.
            const auto& outer = *instance.warp;
            resolved.warp = BeatWarp {outer.tempo, outer.inputOffset, outer.inputScale, outer.beat(first), rate * outer.secondsPerBeat};
        } else if (warp.has_value()) {
            // A linear instance feeds this warp through an affine input.
            const auto& inner = *warp;
            resolved.warp = BeatWarp {inner.tempo, inner.inputOffset + inner.inputScale * (instance.offset - instance.start * instance.rate),
                                      inner.inputScale * instance.rate, 0, inner.secondsPerBeat};
            resolved.warp->startBeat = resolved.warp->beat(first);
        }
        return resolved.valid() ? std::optional<ClipTiming>(resolved) : std::nullopt;
    }
    bool valid() const {
        return std::isfinite(start) && start >= 0 && std::isfinite(length) && length > 0
            && std::isfinite(finish) && finish > start && std::isfinite(offset) && std::isfinite(rate) && rate > 0;
    }
private:
    // Keep the converted boundary itself: start + (end - start) can round to
    // a different value and invent overlaps between exactly adjacent clips.
    double finish, length;
};
}
