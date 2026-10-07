#pragma once

#include "Id.h"
#include "Modulation.h"
#include "Tempo.h"
#include <optional>
#include <string>

namespace motion {
// Reusable modulation sources owned by a composition. One modulator can drive
// any number of properties through routes, so several objects share a single
// clock (an LFO, a random walk or the soundtrack).
struct Modulator {
    Id id = 0;
    std::string name = "Modulator";
    // The waveform, clock and seed; its amount, mode and enabled flag are
    // unused (each route carries its own depth).
    Modulation shape;

    bool operator==(const Modulator&) const = default;
    bool valid() const { return id != 0 && !name.empty() && name.size() <= 120 && shape.valid(); }
    // Oscillators are bipolar (-1..1); soundtrack loudness is unipolar (0..1).
    bool unipolar() const { return shape.waveform == ModulationWaveform::soundtrack; }
};

// A modulator driving one property: value += amount * m (add) or
// value *= 1 + amount * m (multiply), after keys and any link.
struct ModulationRoute {
    Id id = 0;
    Id modulator = 0;
    Id target = 0;
    std::string property;
    double amount = 1;
    ModulationMode mode = ModulationMode::add;

    bool operator==(const ModulationRoute&) const = default;
    bool valid() const {
        return id != 0 && modulator != 0 && target != 0 && !property.empty() && std::isfinite(amount) && amount >= -1000000 && amount <= 1000000
            && (mode == ModulationMode::add || mode == ModulationMode::multiply);
    }
};

// Prepared, immutable evaluation of one modulator in its composition's time.
// All state is precomputed; value() is allocation-free and pure.
struct PreparedModulator {
    Modulation shape;
    double bpm = 120;
    std::optional<Tempo> tempo; // set when the composition has tempo changes
    std::shared_ptr<const SoundtrackEnvelope> soundtrack; // project time

    // `time` is the owning composition's time; `projectTime` reaches the
    // soundtrack, which always runs on the main timeline.
    double value(double time, double projectTime) const {
        if (!std::isfinite(time)) { return 0; }
        if (shape.waveform == ModulationWaveform::soundtrack) { return soundtrack != nullptr ? soundtrack->at(projectTime) : 0.0; }
        return shape.tempoSync && tempo.has_value() ? shape.valueAtBeats(tempo->beats(time)) : shape.value(time, bpm);
    }
};
}
