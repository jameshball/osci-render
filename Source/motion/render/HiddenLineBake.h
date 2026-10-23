#pragma once

#include "../model/PreparedDrawing.h"
#include "../model/ScopeProfile.h"
#include "HiddenLineDepth.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace motion {
struct Project;
}

namespace motion::hidden {
// One video frame with hidden lines removed: the drawings of the clips that
// lose lines, by their index in the prepared composition. A clip not listed
// draws whole; one listed with no shapes is wholly hidden.
struct Frame {
    struct Entry {
        std::size_t clip;
        std::shared_ptr<const PreparedDrawing> drawing;
        // For a point source: the fraction of its path that shows, so its
        // share of the beam is measured as it always is, scaled by it.
        double shown = 1;
    };
    std::vector<Entry> drawings;
    // What each reflecting clip shows of the scene: its drawing's points are
    // in the scene, coloured as they are drawn, and only the camera places
    // them. Drawn as part of that clip, with its share of the beam.
    std::vector<Entry> reflections;
    // The camera's hatching on each surface it shades, likewise in the scene.
    std::vector<Entry> hatching;

    // Drawings are listed in clip order, so found by halving.
    const Entry* entry(std::size_t clip) const {
        const auto found = std::lower_bound(drawings.begin(), drawings.end(), clip, [](const Entry& item, std::size_t value) { return item.clip < value; });
        return found != drawings.end() && found->clip == clip ? &*found : nullptr;
    }
    const PreparedDrawing* drawing(std::size_t clip) const {
        const auto* found = entry(clip);
        return found != nullptr ? found->drawing.get() : nullptr;
    }
};

// A composition's frames with hidden lines removed, baked off the render
// thread at the project's frame rate. Workers claim frames nearest the
// playhead first and publish them; the render thread reads them without
// locks, drawing everything for a frame not yet baked. A bake made after an
// edit takes over the frames of the one before that the edit left as they
// were, so only what changed bakes again.
class Bake {
public:
    Bake(std::shared_ptr<const Project> snapshot, double frameRate, double duration, Reach shown, double sampleRate = 48000)
        : project(std::move(snapshot)), reach(shown), samples(sampleRate), rate(std::isfinite(frameRate) && frameRate > 0 ? frameRate : 60),
          count(std::isfinite(duration) && duration > 0 ? static_cast<std::size_t>(std::ceil(duration * rate - 1.0e-9)) + 1 : 1),
          slots(std::make_unique<std::atomic<const Frame*>[]>(count)), states(std::make_unique<std::atomic<std::uint8_t>[]>(count)),
          kept(std::make_unique<std::shared_ptr<const Frame>[]>(count)), prints(std::make_unique<std::uint64_t[]>(count)) {
        for (std::size_t index = 0; index < count; ++index) {
            slots[index].store(nullptr, std::memory_order_relaxed);
            states[index].store(waiting, std::memory_order_relaxed);
        }
    }
    Bake(const Bake&) = delete;
    Bake& operator=(const Bake&) = delete;

    // The project the composition was prepared from, and what the Scope
    // shows of it, for the bake's workers.
    const std::shared_ptr<const Project> project;
    const Reach reach;
    // How it bakes: a bake takes frames over only from one baked the same way.
    struct Settings {
        // What it bakes: lines a camera hides, light from lights, reflections,
        // surfaces drawn other than by their edges, hatching, or any of them.
        bool hides = true, lights = false, reflects = false, restyles = false, hatches = false;
        // The beam's cycles a second, which size the hatching drawn in one.
        double beamRate = 60;
        // How the Scope draws, which shapes how lines are cut.
        ScopeProfile scope;
        bool operator==(const Settings&) const = default;
    };
    Settings settings;

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

    // Where a frame is: still to bake, being baked, or done (a frame that
    // couldn't be baked is done too, and drawn whole).
    enum class State : std::uint8_t { waiting, baking, done };
    State state(std::size_t frame) const { return static_cast<State>(states[std::min(frame, count - 1)].load(std::memory_order_relaxed)); }

    // The frame workers start from, and go on from: the playhead's, so what
    // is looked at bakes first, then what plays next.
    void focusOn(std::size_t frame) { focus.store(std::min(frame, count - 1), std::memory_order_relaxed); }

    // Workers: the next frame to bake, the first after the focus not yet
    // taken (wrapping to the start), or none left.
    std::optional<std::size_t> claim() {
        const auto from = focus.load(std::memory_order_relaxed);
        // Every frame from the focus to the last one taken from it is taken:
        // the search goes on from there.
        const auto last = hint.load(std::memory_order_relaxed);
        auto step = static_cast<std::size_t>(last >> 32) == from ? static_cast<std::size_t>(last & 0xffffffffu) : 0;
        for (; step < count; ++step) {
            const auto frame = (from + step) % count;
            auto expected = waiting;
            if (states[frame].load(std::memory_order_relaxed) == waiting && states[frame].compare_exchange_strong(expected, baking, std::memory_order_relaxed)) {
                hint.store(hintAt(from, step), std::memory_order_relaxed);
                return frame;
            }
        }
        hint.store(hintAt(from, count), std::memory_order_relaxed);
        return std::nullopt;
    }
    // A frame baked, and the fingerprint (its hash) of what it depends on.
    void publish(std::size_t frame, std::shared_ptr<const Frame> result, std::uint64_t print) {
        if (states[frame].load(std::memory_order_relaxed) == done) { return; }
        prints[frame] = print;
        kept[frame] = std::move(result);
        slots[frame].store(kept[frame].get(), std::memory_order_release);
        states[frame].store(done, std::memory_order_release);
        finished.fetch_add(1, std::memory_order_relaxed);
    }
    // A claimed frame given back unbaked (its worker was stopped), for the
    // next claim to find again.
    void release(std::size_t frame) {
        auto expected = baking;
        if (states[frame].compare_exchange_strong(expected, waiting, std::memory_order_relaxed)) { hint.store(~std::uint64_t {0}, std::memory_order_relaxed); }
    }
    // A frame that could not be baked: it is drawn whole, as before it was.
    void skip(std::size_t frame) {
        if (states[frame].exchange(done, std::memory_order_relaxed) != done) { finished.fetch_add(1, std::memory_order_relaxed); }
    }
    std::size_t remaining() const { return count - std::min(finished.load(std::memory_order_relaxed), count); }
    double progress() const { return static_cast<double>(count - remaining()) / static_cast<double>(count); }
    bool complete() const { return remaining() == 0; }

    // Takes over `previous`'s frames, before any worker starts: a frame
    // whose fingerprint matches one of them is that frame. Only from a bake
    // of the same settings, which no fingerprint holds.
    void inherit(const Bake& previous) {
        if (previous.rate != rate || previous.samples != samples || !(previous.reach == reach) || !(previous.settings == settings)) {
            return;
        }
        for (std::size_t index = 0; index < previous.count; ++index) {
            // A frame's print and drawing are written before it is published.
            if (previous.slots[index].load(std::memory_order_acquire) != nullptr) { reusable.emplace(previous.prints[index], previous.kept[index]); }
        }
    }
    // Workers: an inherited frame with this fingerprint, if any.
    std::shared_ptr<const Frame> inherited(std::uint64_t print) const {
        const auto found = reusable.find(print);
        return found != reusable.end() ? found->second : nullptr;
    }

private:
    static constexpr std::uint8_t waiting = static_cast<std::uint8_t>(State::waiting), baking = static_cast<std::uint8_t>(State::baking), done = static_cast<std::uint8_t>(State::done);
    // The focus and how far from it the last claim was, in one word.
    static std::uint64_t hintAt(std::size_t from, std::size_t step) { return (static_cast<std::uint64_t>(from) << 32) | static_cast<std::uint64_t>(step); }
    const double samples, rate;
    const std::size_t count;
    std::unique_ptr<std::atomic<const Frame*>[]> slots;
    std::unique_ptr<std::atomic<std::uint8_t>[]> states;
    // Each frame's owner and fingerprint, written by the one worker that
    // claimed it, before its slot is published.
    std::unique_ptr<std::shared_ptr<const Frame>[]> kept;
    std::unique_ptr<std::uint64_t[]> prints;
    std::unordered_map<std::uint64_t, std::shared_ptr<const Frame>> reusable;
    std::atomic<std::size_t> focus {0}, finished {0};
    std::atomic<std::uint64_t> hint {0};
};
}
