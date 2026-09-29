#pragma once

#include <array>
#include <cmath>

namespace motion {
struct MidiInstrument {
    double attack = 0, decay = 0, sustain = 1, release = 0;
    // Semitones at full pitch-bend deflection.
    double bendRange = 2;
    bool operator==(const MidiInstrument&) const = default;
    std::array<double, 5> key() const { return {attack, decay, sustain, release, bendRange}; }
    bool valid() const {
        for (const auto seconds : {attack, decay, release}) {
            if (!std::isfinite(seconds) || seconds < 0 || seconds > 30) { return false; }
        }
        return std::isfinite(sustain) && sustain >= 0 && sustain <= 1 && std::isfinite(bendRange) && bendRange >= 0 && bendRange <= 48;
    }
};
}
