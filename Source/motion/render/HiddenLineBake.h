#pragma once

#include "../model/PreparedDrawing.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace motion {
struct Project;
}

namespace motion::hidden {
// One video frame with hidden lines removed: the drawings of the clips that
// lose lines, by their index in the prepared composition. A clip not listed
// draws whole; one listed with no shapes is wholly hidden.
struct Frame {
    std::vector<std::pair<std::size_t, std::shared_ptr<const PreparedDrawing>>> drawings;

    const PreparedDrawing* drawing(std::size_t clip) const {
        for (const auto& [index, drawing] : drawings) {
            if (index == clip) { return drawing.get(); }
        }
        return nullptr;
    }
};

// A composition's frames with hidden lines removed, baked off the render
// thread at the project's frame rate. Workers claim and publish frames; the
// render thread reads them without locks, drawing everything for a frame not
// yet baked. Frames are kept until the bake is destroyed.
class Bake {
public:
    Bake(std::shared_ptr<const Project> snapshot, double frameRate, double duration)
        : project(std::move(snapshot)), rate(std::isfinite(frameRate) && frameRate > 0 ? frameRate : 60),
          count(std::isfinite(duration) && duration > 0 ? static_cast<std::size_t>(std::ceil(duration * rate - 1.0e-9)) + 1 : 1),
          slots(std::make_unique<std::atomic<const Frame*>[]>(count)) {
        for (std::size_t index = 0; index < count; ++index) { slots[index].store(nullptr, std::memory_order_relaxed); }
    }
    ~Bake() {
        for (std::size_t index = 0; index < count; ++index) { delete slots[index].load(std::memory_order_relaxed); }
    }
    Bake(const Bake&) = delete;
    Bake& operator=(const Bake&) = delete;

    // The project the composition was prepared from, for the bake's workers.
    const std::shared_ptr<const Project> project;

    double frameRate() const { return rate; }
    std::size_t frameCount() const { return count; }
    // The frame a time falls in, and the time a frame is baked at (its start).
    std::size_t frameOf(double time) const {
        const auto index = std::isfinite(time) ? std::floor(time * rate + 1.0e-9) : 0.0;
        return static_cast<std::size_t>(std::clamp(index, 0.0, static_cast<double>(count - 1)));
    }
    double timeOf(std::size_t frame) const { return static_cast<double>(frame) / rate; }

    // Render thread: the frame at `time`, if baked.
    const Frame* at(double time) const { return slots[frameOf(time)].load(std::memory_order_acquire); }

    // Workers: the next frame to bake, nearest the start first, or none left.
    std::optional<std::size_t> claim(std::size_t start) {
        const auto claimed = next.fetch_add(1, std::memory_order_relaxed);
        if (claimed >= count) { return std::nullopt; }
        return (start + claimed) % count;
    }
    void publish(std::size_t frame, std::unique_ptr<Frame> result) {
        delete slots[frame].exchange(result.release(), std::memory_order_acq_rel);
        done.fetch_add(1, std::memory_order_relaxed);
    }
    double progress() const { return static_cast<double>(std::min(done.load(std::memory_order_relaxed), count)) / static_cast<double>(count); }
    bool complete() const { return done.load(std::memory_order_relaxed) >= count; }

private:
    const double rate;
    const std::size_t count;
    std::unique_ptr<std::atomic<const Frame*>[]> slots;
    std::atomic<std::size_t> next {0}, done {0};
};
}
