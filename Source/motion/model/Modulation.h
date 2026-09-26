#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace motion {
enum class ModulationWaveform { sine, triangle, saw, square, noiseSmooth, noiseHold };
enum class ModulationMode { add, multiply };

struct Modulation {
    bool enabled = false;
    ModulationWaveform waveform = ModulationWaveform::sine;
    double amount = 0.25;
    double rateHz = 1;
    double phase = 0;
    bool tempoSync = false;
    double beatsPerCycle = 1;
    std::uint32_t seed = 0;
    ModulationMode mode = ModulationMode::add;

    bool operator==(const Modulation&) const = default;
    bool valid() const {
        return static_cast<int>(waveform) >= 0 && static_cast<int>(waveform) <= 5
            && static_cast<int>(mode) >= 0 && static_cast<int>(mode) <= 1
            && std::isfinite(amount) && amount >= -1000000 && amount <= 1000000
            && std::isfinite(rateHz) && rateHz >= 0.001 && rateHz <= 1000
            && std::isfinite(phase) && phase >= 0 && phase <= 1
            && std::isfinite(beatsPerCycle) && beatsPerCycle >= 0.0625 && beatsPerCycle <= 64;
    }

    // Pure owner-time evaluation: no oscillator state, random generator,
    // sample-rate dependency or history, including when scrubbing backwards.
    double value(double time, double bpm = 120) const {
        if (!enabled || !valid() || !std::isfinite(time) || (tempoSync && (!std::isfinite(bpm) || bpm <= 0))) {
            return 0;
        }
        const auto frequency = tempoSync ? bpm / (60 * beatsPerCycle) : rateHz;
        const auto cycles = time * frequency + phase;
        if (!std::isfinite(cycles)) {
            return 0;
        }
        const auto cycle = std::floor(cycles);
        const auto fraction = cycles - cycle;
        switch (waveform) {
            case ModulationWaveform::sine: return std::sin(fraction * 2 * std::numbers::pi);
            case ModulationWaveform::triangle: return 1 - 4 * std::abs(fraction - 0.5);
            case ModulationWaveform::saw: return 2 * fraction - 1;
            case ModulationWaveform::square: return fraction < 0.5 ? 1 : -1;
            case ModulationWaveform::noiseHold: return noise(cycle);
            case ModulationWaveform::noiseSmooth: {
                const auto t = fraction * fraction * (3 - 2 * fraction);
                return noise(cycle) * (1 - t) + noise(cycle + 1) * t;
            }
        }
        return 0;
    }

private:
    double noise(double cycle) const {
        // Wrapping before conversion makes negative and extreme finite times
        // defined on every platform. Unsigned hash overflow is intentional.
        auto wrapped = std::fmod(cycle, 4294967296.0);
        if (wrapped < 0) {
            wrapped += 4294967296.0;
        }
        auto bits = static_cast<std::uint32_t>(wrapped) ^ seed;
        bits += 0x9e3779b9u;
        bits = (bits ^ (bits >> 16)) * 0x21f0aaadu;
        bits = (bits ^ (bits >> 15)) * 0x735a2d97u;
        bits ^= bits >> 15;
        return static_cast<double>(bits) / 4294967295.0 * 2 - 1;
    }
};
}
