#pragma once

#include "../../audio/synth/PreparedNoteVoice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace motion {

// Single audio-thread owner after prepare(). Event timestamps are absolute,
// nondecreasing device samples; transport position never enters this class.
class LiveMidiPerformance {
public:
    struct Selection {
        std::uint64_t note = 0;
        double phase = 0, phaseSpan = 0;
    };
    bool prepare(double rate) {
        reset();
        pitches.fill(std::nullopt);
        DahdsrParams params;
        params.sustainLevel = 1;
        envelope = osci_audio::PreparedVoiceEnvelope::prepare(params, rate);
        if (!envelope) { return false; }
        for (std::size_t pitch = 0; pitch < pitches.size(); ++pitch) {
            pitches[pitch] = osci_audio::PreparedNoteVoice::prepare(*envelope,
                440 * std::exp2((static_cast<double>(pitch) - 69) / 12), 1, 1);
        }
        return true;
    }
    void reset() {
        voices.fill(Voice{});
        pedals.fill(false);
        lastEvent = 0;
        // Identities intentionally survive resets to prevent false continuity.
    }
    bool noteOn(int channel, int pitch, int velocity, std::uint64_t sample) {
        if (!validChannel(channel) || pitch < 0 || pitch > 127 || velocity < 0 || velocity > 127) { return false; }
        if (velocity == 0) { return noteOff(channel, pitch, sample); }
        if (!pitches[static_cast<std::size_t>(pitch)] || !acceptTime(sample)
            || nextIdentity == std::numeric_limits<std::uint64_t>::max()) { return false; }
        Voice* chosen = nullptr;
        for (auto& voice : voices) {
            if (!active(voice, sample)) { chosen = &voice; break; }
        }
        if (chosen == nullptr) {
            // Released tails are stolen before held or sustained voices.
            for (auto& voice : voices) {
                if (chosen == nullptr || (voice.released && !chosen->released)
                    || (voice.released == chosen->released && voice.id < chosen->id)) { chosen = &voice; }
            }
        }
        *chosen = Voice {++nextIdentity, sample, 0, channel, pitch, velocity, true, false};
        return true;
    }
    bool noteOff(int channel, int pitch, std::uint64_t sample) {
        if (!validChannel(channel) || pitch < 0 || pitch > 127 || !envelope || !acceptTime(sample)) { return false; }
        Voice* oldest = nullptr;
        for (auto& voice : voices) {
            if (voice.id != 0 && voice.channel == channel && voice.pitch == pitch && voice.held
                && (oldest == nullptr || voice.id < oldest->id)) { oldest = &voice; }
        }
        if (oldest == nullptr) { return false; }
        oldest->held = false;
        if (!pedals[static_cast<std::size_t>(channel - 1)]) { release(*oldest, sample); }
        return true;
    }
    bool sustain(int channel, bool down, std::uint64_t sample) {
        if (!validChannel(channel) || !envelope || !acceptTime(sample)) { return false; }
        auto& pedal = pedals[static_cast<std::size_t>(channel - 1)];
        if (pedal == down) { return false; }
        pedal = down;
        if (!down) {
            for (auto& voice : voices) {
                if (voice.id != 0 && voice.channel == channel && !voice.held && !voice.released) { release(voice, sample); }
            }
        }
        return true;
    }
    bool allNotesOff(int channel, std::uint64_t sample) {
        if (!validChannel(channel) || !envelope || !acceptTime(sample)) { return false; }
        bool changed = false;
        for (auto& voice : voices) {
            if (voice.id != 0 && voice.channel == channel && voice.held) {
                voice.held = false;
                if (!pedals[static_cast<std::size_t>(channel - 1)]) { release(voice, sample); }
                changed = true;
            }
        }
        return changed;
    }
    bool allSoundOff(int channel) {
        if (!validChannel(channel)) { return false; }
        bool changed = false;
        for (auto& voice : voices) {
            if (voice.id != 0 && voice.channel == channel) { voice = {}; changed = true; }
        }
        return changed;
    }
    Selection select(std::uint64_t sample, double allocationPhase) const {
        if (!envelope || !std::isfinite(allocationPhase) || allocationPhase < 0 || allocationPhase >= 1) { return {}; }
        std::size_t count = 0;
        for (const auto& voice : voices) { if (active(voice, sample)) { ++count; } }
        auto cursor = allocationPhase * static_cast<double>(count);
        for (const auto& voice : voices) {
            if (!active(voice, sample)) { continue; }
            const auto gain = envelopeValue(voice, sample).gain * (voice.velocity / 127.0);
            if (cursor < gain) {
                const auto value = pitches[static_cast<std::size_t>(voice.pitch)]->at(sample - voice.start,
                    voice.released ? voice.heldSamples : std::numeric_limits<std::uint64_t>::max());
                return {voice.id, value.phase, value.phaseSpan};
            }
            cursor -= gain;
        }
        return {}; // Velocity leaves unused drawing time dark, without scaling XYZ.
    }
private:
    struct Voice {
        std::uint64_t id = 0, start = 0, heldSamples = 0;
        int channel = 0, pitch = 0, velocity = 0;
        bool held = false, released = false;
    };
    static bool validChannel(int channel) { return channel >= 1 && channel <= 16; }
    bool acceptTime(std::uint64_t sample) {
        if (sample < lastEvent) { return false; }
        lastEvent = sample;
        return true;
    }
    static void release(Voice& voice, std::uint64_t sample) {
        voice.heldSamples = sample - voice.start;
        voice.released = true;
    }
    osci_audio::PreparedVoiceEnvelope::Value envelopeValue(const Voice& voice, std::uint64_t sample) const {
        auto age = sample - voice.start;
        // UINT64_MAX is the kernel's unreleased sentinel. At the final clock
        // tick evaluate held gain one tick earlier; oscillator phase stays exact.
        if (!voice.released && age == std::numeric_limits<std::uint64_t>::max()) { --age; }
        return envelope->at(age, voice.released ? voice.heldSamples : std::numeric_limits<std::uint64_t>::max());
    }
    bool active(const Voice& voice, std::uint64_t sample) const {
        return voice.id != 0 && sample >= voice.start && envelope
            && envelopeValue(voice, sample).stage != DahdsrStage::Done;
    }
    std::array<Voice, 32> voices {};
    std::array<bool, 16> pedals {};
    std::array<std::optional<osci_audio::PreparedNoteVoice>, 128> pitches {};
    std::optional<osci_audio::PreparedVoiceEnvelope> envelope;
    std::uint64_t nextIdentity = 0, lastEvent = 0;
};
}
