#pragma once

#include "PreparedMidiInstrument.h"
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
    bool prepare(double rate, MidiInstrument settings = {}) {
        const auto prepared = PreparedMidiInstrument::prepare(settings, rate);
        if (!prepared) { reset(); instrument = {}; return false; }
        useInstrument(*prepared);
        return true;
    }
    bool usesInstrument(const PreparedMidiInstrument& value) const {
        return instrument.sampleRate == value.sampleRate && instrument.settings == value.settings && instrument.envelope.has_value();
    }
    void useInstrument(const PreparedMidiInstrument& value) {
        reset();
        instrument = value;
    }
    void reset() {
        voices.fill(Voice{});
        pedals.fill(false);
        bends.fill(Bend{});
        expression.fill(1.0);
        lastEvent = 0;
        // Identities intentionally survive resets to prevent false continuity.
    }
    bool noteOn(int channel, int pitch, int velocity, std::uint64_t sample) {
        if (!validChannel(channel) || pitch < 0 || pitch > 127 || velocity < 0 || velocity > 127) { return false; }
        if (velocity == 0) { return noteOff(channel, pitch, sample); }
        if (!instrument.pitches[static_cast<std::size_t>(pitch)] || !acceptTime(sample)
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
        *chosen = Voice {++nextIdentity, sample, 0, channel, pitch, velocity, true, false, bends[static_cast<std::size_t>(channel - 1)].integral(sample)};
        return true;
    }
    bool noteOff(int channel, int pitch, std::uint64_t sample) {
        if (!validChannel(channel) || pitch < 0 || pitch > 127 || !instrument.envelope || !acceptTime(sample)) { return false; }
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
        if (!validChannel(channel) || !instrument.envelope || !acceptTime(sample)) { return false; }
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
    // Pitch bend (-8192..8191) glides every voice on the channel by up to the
    // instrument's bend range; phase keeps integrating the bent frequency.
    bool pitchBend(int channel, int value, std::uint64_t sample) {
        if (!validChannel(channel) || value < -8192 || value > 8191 || !acceptTime(sample)) { return false; }
        auto& bend = bends[static_cast<std::size_t>(channel - 1)];
        bend.anchor = bend.integral(sample);
        bend.anchorSample = sample;
        bend.factor = std::exp2(instrument.settings.bendRange * (value / 8192.0) / 12);
        bend.bent = true;
        return true;
    }
    // Expression (CC 11) scales each voice's share of the drawing time.
    bool setExpression(int channel, int value) {
        if (!validChannel(channel) || value < 0 || value > 127) { return false; }
        expression[static_cast<std::size_t>(channel - 1)] = value / 127.0;
        return true;
    }
    std::size_t activeCount(std::uint64_t sample) const {
        std::size_t count = 0;
        for (const auto& voice : voices) { if (active(voice, sample)) { ++count; } }
        return count;
    }
    bool allNotesOff(int channel, std::uint64_t sample) {
        if (!validChannel(channel) || !instrument.envelope || !acceptTime(sample)) { return false; }
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
    // Reset all controllers (CC 121): centre the bend, full expression.
    bool resetControllers(int channel, std::uint64_t sample) {
        if (!validChannel(channel)) { return false; }
        pitchBend(channel, 0, sample);
        bends[static_cast<std::size_t>(channel - 1)].bent = false;
        expression[static_cast<std::size_t>(channel - 1)] = 1.0;
        sustain(channel, false, sample);
        return true;
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
        if (!instrument.envelope || !std::isfinite(allocationPhase) || allocationPhase < 0 || allocationPhase >= 1) { return {}; }
        std::size_t count = 0;
        for (const auto& voice : voices) { if (active(voice, sample)) { ++count; } }
        auto cursor = allocationPhase * static_cast<double>(count);
        for (const auto& voice : voices) {
            if (!active(voice, sample)) { continue; }
            const auto channel = static_cast<std::size_t>(voice.channel - 1);
            const auto gain = envelopeValue(voice, sample).gain * (voice.velocity / 127.0) * expression[channel];
            if (cursor < gain) {
                const auto value = instrument.pitches[static_cast<std::size_t>(voice.pitch)]->at(sample - voice.start,
                    voice.released ? voice.heldSamples : std::numeric_limits<std::uint64_t>::max());
                const auto& bend = bends[channel];
                if (!bend.bent) { return {voice.id, value.phase, value.phaseSpan}; }
                const auto step = value.frequency / instrument.sampleRate;
                const auto cycles = step * (bend.integral(sample) - voice.integralStart);
                return {voice.id, cycles - std::floor(cycles), step * bend.factor};
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
        double integralStart = 0; // bend integral at note-on
    };
    struct Bend {
        double factor = 1, anchor = 0;
        std::uint64_t anchorSample = 0;
        bool bent = false;
        double integral(std::uint64_t sample) const {
            return anchor + (static_cast<double>(sample) - static_cast<double>(anchorSample)) * factor;
        }
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
        return instrument.envelope->at(age, voice.released ? voice.heldSamples : std::numeric_limits<std::uint64_t>::max());
    }
    bool active(const Voice& voice, std::uint64_t sample) const {
        return voice.id != 0 && sample >= voice.start && instrument.envelope
            && envelopeValue(voice, sample).stage != DahdsrStage::Done;
    }
    std::array<Voice, 32> voices {};
    std::array<bool, 16> pedals {};
    std::array<Bend, 16> bends {};
    std::array<double, 16> expression {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    PreparedMidiInstrument instrument;
    std::uint64_t nextIdentity = 0, lastEvent = 0;
};
}
