#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace motion {
// A tempo change takes effect at a beat and holds until the next change. A
// ramped change glides there instead: tempo moves linearly in beats from the
// previous point (or the initial tempo) and arrives at `bpm` on `beat`.
struct TempoChange {
    double beat = 0;
    double bpm = 120;
    bool ramp = false;
    bool operator==(const TempoChange&) const = default;
    bool valid() const { return std::isfinite(beat) && beat > 0 && std::isfinite(bpm) && bpm >= 1 && bpm <= 1000; }
};

// The project's musical clock: an initial tempo from beat 0 (and before it),
// then stepped or ramped changes. Conversions are exact and invertible (ramps
// integrate in closed form). Construction from a plain BPM is explicit so no
// caller silently ignores a tempo map. A map keeps the seconds at each change,
// so a conversion is a binary search and one segment, free of allocation.
class Tempo {
public:
    explicit Tempo(double initialBpm = 120, std::shared_ptr<const std::vector<TempoChange>> tempoChanges = nullptr)
        : bpm(initialBpm), changes(std::move(tempoChanges)) {
        if (changes != nullptr && changes->empty()) { changes.reset(); }
        if (changes != nullptr) {
            // elapsed[i]: seconds at the start of change i's segment; the last
            // entry is the seconds at the final change.
            auto sums = std::make_shared<std::vector<double>>();
            sums->reserve(changes->size() + 1);
            double seconds = 0, from = 0, current = bpm;
            sums->push_back(seconds);
            for (const auto& change : *changes) {
                seconds += segmentSeconds(from, current, change);
                sums->push_back(seconds);
                from = change.beat;
                current = change.bpm;
            }
            elapsed = std::move(sums);
        }
    }

    double initialBpm() const { return bpm; }
    bool constant() const { return changes == nullptr; }
    bool valid() const {
        if (!std::isfinite(bpm) || bpm < 1 || bpm > 1000) { return false; }
        if (changes == nullptr) { return true; }
        double previous = 0, previousBpm = bpm;
        for (const auto& change : *changes) {
            if (!change.valid() || change.beat <= previous) { return false; }
            // A ramp over a vanishing span (a denormal gap) has no finite slope.
            const auto slope = (change.bpm - previousBpm) / (change.beat - previous);
            if (change.ramp && !std::isfinite(slope)) { return false; }
            previous = change.beat;
            previousBpm = change.bpm;
        }
        return true;
    }

    // Before beat 0 the initial tempo holds, whatever the first change is.
    double seconds(double beat) const {
        if (changes == nullptr || !std::isfinite(beat) || beat < 0) { return beat * 60 / bpm; }
        const auto& list = *changes;
        // The first change at or after the beat.
        const auto index = static_cast<std::size_t>(std::lower_bound(list.begin(), list.end(), beat,
            [](const TempoChange& change, double value) { return change.beat < value; }) - list.begin());
        const auto from = index == 0 ? 0.0 : list[index - 1].beat;
        const auto current = index == 0 ? bpm : list[index - 1].bpm;
        const auto seconds = (*elapsed)[index];
        if (index == list.size()) { return seconds + (beat - from) * 60 / current; }
        const auto& change = list[index];
        return seconds + (change.ramp ? rampSeconds(from, current, change, beat) : (beat - from) * 60 / current);
    }
    double beats(double time) const {
        if (changes == nullptr || !std::isfinite(time) || time < 0) { return time * bpm / 60; }
        const auto& list = *changes;
        // The first segment whose end is at or after the time.
        const auto ends = elapsed->begin() + 1;
        const auto index = static_cast<std::size_t>(std::lower_bound(ends, elapsed->end(), time) - ends);
        const auto from = index == 0 ? 0.0 : list[index - 1].beat;
        const auto current = index == 0 ? bpm : list[index - 1].bpm;
        const auto seconds = (*elapsed)[index];
        if (index == list.size()) { return from + (time - seconds) * current / 60; }
        const auto& change = list[index];
        return change.ramp ? rampBeat(from, current, change, time - seconds) : from + (time - seconds) * current / 60;
    }
    // Tempo at a beat (ramps interpolate linearly in beats).
    double bpmAtBeat(double beat) const {
        if (changes == nullptr || beat < 0) { return bpm; }
        const auto& list = *changes;
        // The first change after the beat.
        const auto index = static_cast<std::size_t>(std::upper_bound(list.begin(), list.end(), beat,
            [](double value, const TempoChange& change) { return value < change.beat; }) - list.begin());
        const auto from = index == 0 ? 0.0 : list[index - 1].beat;
        const auto current = index == 0 ? bpm : list[index - 1].bpm;
        if (index == list.size()) { return current; }
        const auto& change = list[index];
        return change.ramp ? current + (change.bpm - current) * (beat - from) / (change.beat - from) : current;
    }
    double bpmAt(double time) const { return changes == nullptr ? bpm : bpmAtBeat(beats(time)); }
    // The single tempo that spans [firstBeat, lastBeat] in the same time.
    double averageBpm(double firstBeat, double lastBeat) const {
        if (changes == nullptr) { return bpm; }
        const auto span = seconds(lastBeat) - seconds(firstBeat);
        const auto average = (lastBeat - firstBeat) * 60 / span;
        return std::isfinite(average) && average > 0 ? average : bpmAtBeat(firstBeat);
    }

private:
    // Seconds from `from` to `beat` inside a ramp that starts at `start` BPM
    // and reaches change.bpm at change.beat: tempo v(b) is linear in beats, so
    // t = 60 / k * ln(v(b) / v0) with k the slope in BPM per beat.
    static double rampSeconds(double from, double start, const TempoChange& change, double beat) {
        const auto slope = (change.bpm - start) / (change.beat - from);
        const auto x = slope * (beat - from) / start;
        if (std::abs(x) < 1e-12) { return (beat - from) * 60 / start; }
        return 60 / slope * std::log1p(x);
    }
    static double segmentSeconds(double from, double start, const TempoChange& change) {
        return change.ramp ? rampSeconds(from, start, change, change.beat) : (change.beat - from) * 60 / start;
    }
    static double rampBeat(double from, double start, const TempoChange& change, double elapsed) {
        const auto slope = (change.bpm - start) / (change.beat - from);
        const auto y = slope * elapsed / 60;
        if (std::abs(y) < 1e-12) { return from + elapsed * start / 60; }
        return from + start * std::expm1(y) / slope;
    }

    double bpm;
    std::shared_ptr<const std::vector<TempoChange>> changes;
    std::shared_ptr<const std::vector<double>> elapsed;
};
}
