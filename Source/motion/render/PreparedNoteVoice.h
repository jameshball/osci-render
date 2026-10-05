#pragma once

#include "../../audio/modulation/DahdsrEnvelope.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

namespace motion {
// Immutable musical state, independent of source ownership, project parameters
// and the product's choice of spatial mixing or drawing-budget allocation.
class PreparedVoiceEnvelope {
public:
    struct Value {
        float gain = 0;
        DahdsrStage stage = DahdsrStage::Done;
    };

    // Prepare off the audio thread. Count transitions using the same repeated
    // dt additions as DahdsrState; ceil(seconds * rate) can differ by one sample.
    // Work is bounded by the public envelope limits and can be cancelled.
    static std::optional<PreparedVoiceEnvelope> prepare(const DahdsrParams& params, double sampleRate, const std::atomic<bool>* cancel = nullptr) {
        if (!std::isfinite(sampleRate) || sampleRate < 1 || sampleRate > 768000) { return std::nullopt; }
        for (const auto duration : {params.delaySeconds, params.attackSeconds, params.holdSeconds, params.decaySeconds, params.releaseSeconds}) {
            if (!std::isfinite(duration) || duration < 0 || duration > osci_audio::kDahdsrTimeMaxSeconds) { return std::nullopt; }
        }
        for (const auto level : {params.attackLevel, params.sustainLevel}) {
            if (!std::isfinite(level) || level < 0 || level > 1) { return std::nullopt; }
        }
        for (const auto curve : {params.attackCurve, params.decayCurve, params.releaseCurve}) {
            if (!std::isfinite(curve) || std::abs(curve) > osci_audio::kMaxPower) { return std::nullopt; }
        }
        PreparedVoiceEnvelope result;
        result.samples.fill(0);
        result.params = params;
        result.dt = 1 / sampleRate;
        const std::array<double, 5> durations {params.delaySeconds, params.attackSeconds, params.holdSeconds, params.decaySeconds, params.releaseSeconds};
        for (std::size_t stage = 0; stage < durations.size(); ++stage) {
            if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) { return std::nullopt; }
            if ((stage == 0 || stage == 2) && durations[stage] == 0) { continue; }
            double elapsed = 0;
            do {
                elapsed += result.dt;
                ++result.samples[stage];
                if ((result.samples[stage] & 4095) == 0 && cancel != nullptr && cancel->load(std::memory_order_relaxed)) { return std::nullopt; }
            } while (elapsed < durations[stage]);
        }
        return result;
    }

    // Sample indices are relative to note-on. heldSamples is the exclusive
    // note-off index; release captures the preceding held sample, just as the
    // live voice does when beginRelease() runs before the next render sample.
    Value at(std::uint64_t ageSamples, std::uint64_t heldSamples) const {
        if (ageSamples < heldSamples) { return heldAt(ageSamples); }
        const auto releaseAge = ageSamples - heldSamples;
        if (releaseAge >= releaseSamples() - 1) { return {}; }
        const auto level = heldSamples == 0 ? 0.0f : heldAt(heldSamples - 1).gain;
        return {evaluateDahdsrStage(DahdsrStage::Release, params, static_cast<double>(releaseAge) * dt, level), DahdsrStage::Release};
    }

    std::uint64_t releaseSamples() const { return samples[4]; }
    double sampleRate() const { return 1 / dt; }

private:
    Value heldAt(std::uint64_t age) const {
        constexpr std::array<DahdsrStage, 5> stages {DahdsrStage::Delay, DahdsrStage::Attack, DahdsrStage::Hold, DahdsrStage::Decay, DahdsrStage::Sustain};
        for (std::size_t index = 0; index < 4; ++index) {
            if (age < samples[index]) {
                auto after = index;
                if (age == samples[index] - 1) {
                    do { ++after; } while (after < 4 && samples[after] == 0);
                }
                return {evaluateDahdsrStage(stages[index], params, static_cast<double>(age) * dt), stages[after]};
            }
            age -= samples[index];
        }
        return {static_cast<float>(params.sustainLevel), DahdsrStage::Sustain};
    }
    DahdsrParams params;
    double dt = 1.0 / 48000;
    std::array<std::uint64_t, 5> samples {0, 1, 0, 1, 1};
};

struct NoteVoiceValue {
    double frequency = 0, phase = 0, phaseSpan = 0;
    float envelope = 0, velocityGain = 1;
    DahdsrStage stage = DahdsrStage::Done;
    bool active() const { return stage != DahdsrStage::Done; }
};

class PreparedNoteVoice {
public:
    // Frequency is resolved by the owning tuning service, not a DAW parameter.
    // Velocity tracking follows ShapeVoice: -1 inverted, 0 none, +1 full.
    static std::optional<PreparedNoteVoice> prepare(const PreparedVoiceEnvelope& envelope, double frequency, float velocity, float velocityTracking) {
        if (!std::isfinite(frequency) || frequency <= 0 || frequency > envelope.sampleRate() / 2
            || !std::isfinite(velocity) || velocity < 0 || velocity > 1
            || !std::isfinite(velocityTracking) || velocityTracking < -1 || velocityTracking > 1) { return std::nullopt; }
        return PreparedNoteVoice(envelope, frequency, osci_audio::voiceVelocityGain(velocity, velocityTracking));
    }

    NoteVoiceValue at(std::uint64_t ageSamples, std::uint64_t heldSamples) const {
        const auto value = envelope.at(ageSamples, heldSamples);
        auto remaining = ageSamples;
        auto step = frequency / envelope.sampleRate();
        double phase = 0;
        // Modular multiplication keeps every intermediate in [0, 1). Casting
        // the whole age to double loses adjacent samples beyond 2^53, and even
        // much smaller products gradually discard fractional phase precision.
        while (remaining != 0) {
            if ((remaining & 1) != 0) {
                phase += step;
                if (phase >= 1) { phase -= 1; }
            }
            remaining >>= 1;
            step += step;
            if (step >= 1) { step -= 1; }
        }
        return {frequency, phase, frequency / envelope.sampleRate(), value.gain, velocityGain, value.stage};
    }

private:
    PreparedNoteVoice(PreparedVoiceEnvelope envelope, double frequency, float velocityGain)
        : envelope(std::move(envelope)), frequency(frequency), velocityGain(velocityGain) {}
    PreparedVoiceEnvelope envelope;
    double frequency;
    float velocityGain;
};
}
