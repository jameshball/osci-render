#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace motion {
// A tempo change takes effect at a beat and holds until the next change.
struct TempoChange {
    double beat = 0;
    double bpm = 120;
    bool operator==(const TempoChange&) const = default;
    bool valid() const { return std::isfinite(beat) && beat > 0 && std::isfinite(bpm) && bpm >= 1 && bpm <= 1000; }
};

// The project's musical clock: an initial tempo from beat 0, then stepped
// changes. Conversions are exact and invertible. Construction from a plain
// BPM is explicit so no caller silently ignores a tempo map.
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
            if (beat <= change.beat) { break; }
            seconds += (change.beat - from) * 60 / current;
            from = change.beat;
            current = change.bpm;
        }
        return seconds + (beat - from) * 60 / current;
    }
    double beats(double time) const {
        if (changes == nullptr || !std::isfinite(time)) { return time * bpm / 60; }
        double seconds = 0, from = 0, current = bpm;
        for (const auto& change : *changes) {
            const auto next = seconds + (change.beat - from) * 60 / current;
            if (time <= next) { break; }
            seconds = next;
            from = change.beat;
            current = change.bpm;
        }
        return from + (time - seconds) * current / 60;
    }
    double bpmAt(double time) const {
        if (changes == nullptr) { return bpm; }
        const auto beat = beats(time);
        auto current = bpm;
        for (const auto& change : *changes) {
            if (beat < change.beat) { break; }
            current = change.bpm;
        }
        return current;
    }
    // The single tempo that spans [firstBeat, lastBeat] in the same time.
    double averageBpm(double firstBeat, double lastBeat) const {
        if (changes == nullptr) { return bpm; }
        const auto span = seconds(lastBeat) - seconds(firstBeat);
        const auto average = (lastBeat - firstBeat) * 60 / span;
        return std::isfinite(average) && average > 0 ? average : bpmAt(seconds(firstBeat));
    }
    // The same map at another initial tempo, change beats and tempos intact.
    Tempo withInitial(double initial) const { return Tempo(initial, changes); }

private:
    double bpm;
    std::shared_ptr<const std::vector<TempoChange>> changes;
};
}
