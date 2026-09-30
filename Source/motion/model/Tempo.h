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

// The project's musical clock: an initial tempo from beat 0, then stepped or
// ramped changes. Conversions are exact and invertible (ramps integrate in
// closed form). Construction from a plain BPM is explicit so no caller
// silently ignores a tempo map.
class Tempo {
public:
    explicit Tempo(double initialBpm = 120, std::shared_ptr<const std::vector<TempoChange>> tempoChanges = nullptr)
        : bpm(initialBpm), changes(std::move(tempoChanges)) {
        if (changes != nullptr && changes->empty()) { changes.reset(); }
    }

    double initialBpm() const { return bpm; }
    bool constant() const { return changes == nullptr; }
    bool valid() const {
        if (!std::isfinite(bpm) || bpm < 1 || bpm > 1000) { return false; }
        if (changes == nullptr) { return true; }
        double previous = 0;
        for (const auto& change : *changes) {
            if (!change.valid() || change.beat <= previous) { return false; }
            previous = change.beat;
        }
        return true;
    }
    const std::vector<TempoChange>* tempoChanges() const { return changes.get(); }

    double seconds(double beat) const {
        if (changes == nullptr || !std::isfinite(beat)) { return beat * 60 / bpm; }
        double seconds = 0, from = 0, current = bpm;
        for (const auto& change : *changes) {
            if (beat <= change.beat) {
                return seconds + (change.ramp ? rampSeconds(from, current, change, beat) : (beat - from) * 60 / current);
            }
            seconds += change.ramp ? rampSeconds(from, current, change, change.beat) : (change.beat - from) * 60 / current;
            from = change.beat;
            current = change.bpm;
        }
        return seconds + (beat - from) * 60 / current;
    }
    double beats(double time) const {
        if (changes == nullptr || !std::isfinite(time)) { return time * bpm / 60; }
        double seconds = 0, from = 0, current = bpm;
        for (const auto& change : *changes) {
            const auto length = change.ramp ? rampSeconds(from, current, change, change.beat) : (change.beat - from) * 60 / current;
            if (time <= seconds + length) {
                return change.ramp ? rampBeat(from, current, change, time - seconds) : from + (time - seconds) * current / 60;
            }
            seconds += length;
            from = change.beat;
            current = change.bpm;
        }
        return from + (time - seconds) * current / 60;
    }
    // Tempo at a beat (ramps interpolate linearly in beats).
    double bpmAtBeat(double beat) const {
        if (changes == nullptr) { return bpm; }
        double from = 0, current = bpm;
        for (const auto& change : *changes) {
            if (beat < change.beat) {
                return change.ramp ? current + (change.bpm - current) * (beat - from) / (change.beat - from) : current;
            }
            from = change.beat;
            current = change.bpm;
        }
        return current;
    }
    double bpmAt(double time) const { return changes == nullptr ? bpm : bpmAtBeat(beats(time)); }
    // The single tempo that spans [firstBeat, lastBeat] in the same time.
    double averageBpm(double firstBeat, double lastBeat) const {
        if (changes == nullptr) { return bpm; }
        const auto span = seconds(lastBeat) - seconds(firstBeat);
        const auto average = (lastBeat - firstBeat) * 60 / span;
        return std::isfinite(average) && average > 0 ? average : bpmAtBeat(firstBeat);
    }
    // The same map at another initial tempo, change beats and tempos intact.
    Tempo withInitial(double initial) const { return Tempo(initial, changes); }

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
    static double rampBeat(double from, double start, const TempoChange& change, double elapsed) {
        const auto slope = (change.bpm - start) / (change.beat - from);
        const auto y = slope * elapsed / 60;
        if (std::abs(y) < 1e-12) { return from + elapsed * start / 60; }
        return from + start * std::expm1(y) / slope;
    }

    double bpm;
    std::shared_ptr<const std::vector<TempoChange>> changes;
};
}
