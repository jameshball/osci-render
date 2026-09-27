#pragma once

#include <array>
#include <cmath>

namespace motion {
struct MidiInstrument {
    double attack = 0, decay = 0, sustain = 1, release = 0;
    bool operator==(const MidiInstrument&) const = default;
    std::array<double, 4> key() const { return {attack, decay, sustain, release}; }
    bool valid() const {
        for (const auto seconds : {attack, decay, release}) {
            if (!std::isfinite(seconds) || seconds < 0 || seconds > 30) { return false; }
        }
        return std::isfinite(sustain) && sustain >= 0 && sustain <= 1;
    }
};
}
