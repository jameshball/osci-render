#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace motion {
// The stretches of a composition an export draws, one after another: all of
// it, or for stills a short run up to each chosen time, long enough for the
// Scope's persistence to build as it does playing, so a few frames of a long
// project render in seconds.
struct Stretches {
    struct Stretch {
        double start = 0, end = 0;
    };
    std::vector<Stretch> list;

    static Stretches whole(double duration) { return {{{0, std::max(0.0, duration)}}}; }
    // `lead` seconds up to each time, and two frames (at `frameRate`) past it,
    // so the frame at the time is drawn whole.
    static Stretches upTo(const std::vector<double>& times, double frameRate, double lead = .3) {
        Stretches result;
        const auto frame = 1 / (frameRate > 0 ? frameRate : 60.0);
        for (const auto time : times) {
            const auto at = std::max(0.0, time);
            result.list.push_back({std::max(0.0, at - lead), at + 2 * frame});
        }
        return result;
    }

    double duration() const {
        double total = 0;
        for (const auto& stretch : list) { total += stretch.end - stretch.start; }
        return total;
    }
    // The composition's time `seconds` into the export.
    double timeAt(double seconds) const {
        for (const auto& stretch : list) {
            const auto length = stretch.end - stretch.start;
            if (seconds < length) { return stretch.start + seconds; }
            seconds -= length;
        }
        return list.empty() ? 0.0 : list.back().end;
    }
    // Where the composition's `time`, in stretch `index`, is in the export.
    double exportTime(std::size_t index, double time) const {
        double offset = 0;
        for (std::size_t before = 0; before < index && before < list.size(); ++before) { offset += list[before].end - list[before].start; }
        return index < list.size() ? offset + (time - list[index].start) : offset;
    }
    // The starts of the frames (at `frameRate`) the stretches draw, to bake.
    std::vector<double> frameTimes(double frameRate) const {
        const auto rate = frameRate > 0 ? frameRate : 60.0;
        std::vector<double> times;
        for (const auto& stretch : list) {
            for (auto frame = std::floor(stretch.start * rate); frame / rate < stretch.end; ++frame) { times.push_back(frame / rate); }
        }
        return times;
    }
};
}
