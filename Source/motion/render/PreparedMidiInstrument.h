#pragma once

#include "../model/Cancellation.h"
#include "../model/MidiInstrument.h"
#include "PreparedNoteVoice.h"

namespace motion {
// The note a drawing-time allocation lands on, and where its oscillator is.
struct MidiSelection {
    std::uint64_t note = 0;
    double phase = 0, phaseSpan = 0;
};

// Fixed-size immutable instrument. Preparation may count envelope samples and
// belongs on the preparation worker; installing it on the audio thread only copies values.
struct PreparedMidiInstrument {
    MidiInstrument settings;
    double sampleRate = 0;
    std::optional<motion::PreparedVoiceEnvelope> envelope;

    static std::optional<PreparedMidiInstrument> prepare(MidiInstrument settings, double rate, const std::atomic<bool>* cancel = nullptr) {
        if (!settings.valid()) { return std::nullopt; }
        DahdsrParams params;
        params.attackSeconds = settings.attack;
        params.decaySeconds = settings.decay;
        params.sustainLevel = settings.sustain;
        params.releaseSeconds = settings.release;
        PreparedMidiInstrument result;
        result.settings = settings;
        result.sampleRate = rate;
        result.envelope = motion::PreparedVoiceEnvelope::prepare(params, rate, cancel);
        if (!result.envelope) { return std::nullopt; }
        return result;
    }

    static double frequency(int pitch) { return 440 * std::exp2((pitch - 69) / 12.0); }
    // Pitches above the output's Nyquist limit do not play.
    bool plays(int pitch) const { return envelope.has_value() && pitch >= 0 && pitch <= 127 && frequency(pitch) <= sampleRate / 2; }
    // A pitch-bend position (-1 to 1) as a frequency factor.
    double bendFactor(double normalised) const { return std::exp2(settings.bendRange * normalised / 12); }

    // `note` sounding `pitch`, `age` samples after note-on.
    MidiSelection voice(std::uint64_t note, int pitch, std::uint64_t age) const {
        const auto step = frequency(pitch) / sampleRate;
        return {note, motion::PreparedNoteVoice::notePhase(step, age), step};
    }
    // Under pitch bend the phase integrates the bent frequency, so bends glide
    // rather than jump: `bentSamples` is that integral since note-on in unbent
    // samples, and `factor` the bend now.
    MidiSelection bentVoice(std::uint64_t note, int pitch, double bentSamples, double factor) const {
        const auto step = frequency(pitch) / sampleRate;
        const auto cycles = step * bentSamples;
        return {note, cycles - std::floor(cycles), step * factor};
    }
};
}
