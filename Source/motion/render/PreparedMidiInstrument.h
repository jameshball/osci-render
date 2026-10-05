#pragma once

#include "../model/Cancellation.h"
#include "../model/MidiInstrument.h"
#include "../../audio/synth/PreparedNoteVoice.h"

namespace motion {
// Fixed-size immutable instrument. Preparation may count envelope samples and
// belongs on the preparation worker; installing it on the audio thread only copies values.
struct PreparedMidiInstrument {
    MidiInstrument settings;
    double sampleRate = 0;
    std::optional<osci_audio::PreparedVoiceEnvelope> envelope;
    std::array<std::optional<osci_audio::PreparedNoteVoice>, 128> pitches {};

    static std::optional<osci_audio::PreparedVoiceEnvelope> prepareEnvelope(MidiInstrument settings, double rate, const std::atomic<bool>* cancel = nullptr) {
        if (!settings.valid()) { return std::nullopt; }
        DahdsrParams params;
        params.attackSeconds = settings.attack;
        params.decaySeconds = settings.decay;
        params.sustainLevel = settings.sustain;
        params.releaseSeconds = settings.release;
        return osci_audio::PreparedVoiceEnvelope::prepare(params, rate, cancel);
    }
    static std::optional<PreparedMidiInstrument> prepare(MidiInstrument settings, double rate, const std::atomic<bool>* cancel = nullptr) {
        PreparedMidiInstrument result;
        result.settings = settings; result.sampleRate = rate;
        result.envelope = prepareEnvelope(settings, rate, cancel);
        if (!result.envelope) { return std::nullopt; }
        for (std::size_t pitch = 0; pitch < result.pitches.size(); ++pitch) {
            if (cancelled(cancel)) { return std::nullopt; }
            result.pitches[pitch] = osci_audio::PreparedNoteVoice::prepare(*result.envelope,
                440 * std::exp2((static_cast<double>(pitch) - 69) / 12), 1, 1);
        }
        return result;
    }
};
}
