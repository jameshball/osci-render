#pragma once

#include "Cancellation.h"
#include "ClipTiming.h"
#include "MidiNotes.h"
#include "../render/MidiRecording.h"
#include <deque>

namespace motion {
// Off-thread conversion of a take into notes, sustain-held note lengths, and
// persistent controller changes (pitch bend and continuous controllers). The
// warning count covers messages with no stored meaning (aftertouch, program).
struct MidiTakeNotes {
    struct Result {
        std::shared_ptr<const MidiNotes> source;
        std::size_t addedCount = 0, ignoredControllerCount = 0;
        std::string error;
        explicit operator bool() const { return source != nullptr; }
    };
    // `clock` is the target clip's timing: under a tempo map a musical clip's
    // beats follow the map, so each event is read through it exactly.
    static Result convert(const MidiRecording::Take& take, std::shared_ptr<const MidiNotes> base = {}, const std::atomic<bool>* cancel = nullptr, const ClipTiming* clock = nullptr) try {
        const auto beatAt = [&](std::uint64_t sample) {
            if (clock == nullptr || !clock->warp.has_value()) { return take.config.beatAt(sample); }
            return clock->localTime(static_cast<double>(sample) / take.config.sampleRate) * take.config.sourceBpm / 60;
        };
        if (cancelled(cancel)) { return {nullptr, 0, 0, "MIDI conversion cancelled."}; }
        if (take.failure != MidiRecording::Failure::none || !take.config.valid()
            || take.firstSample < take.config.firstSample || take.endSample > take.config.endSample || take.endSample <= take.firstSample
            || take.events.size() > MidiRecording::maximumEvents) {
            return {nullptr, 0, 0, "Invalid or failed MIDI take."};
        }
        auto previous = take.firstSample;
        for (const auto& event : take.events) {
            if (cancelled(cancel)) { return {nullptr, 0, 0, "MIDI conversion cancelled."}; }
            const auto kind = event.bytes[0] & 0xf0;
            const auto length = kind == 0xc0 || kind == 0xd0 ? 2 : 3;
            if (event.sample < previous || event.sample >= take.endSample || kind < 0x80 || kind > 0xe0
                || event.size != length || event.bytes[1] > 127 || (length == 3 && event.bytes[2] > 127)) {
                return {nullptr, 0, 0, "Invalid MIDI event ordering, range or bytes."};
            }
            previous = event.sample;
        }
        std::vector<MidiNote> notes = base != nullptr ? base->notes() : std::vector<MidiNote>{};
        std::vector<MidiControl> controls = base != nullptr ? base->controls() : std::vector<MidiControl>{};
        const auto originalCount = notes.size();
        std::unordered_set<std::uint64_t> used;
        for (const auto& note : notes) { used.insert(note.id); }
        std::uint64_t nextId = 1;
        struct Held { double start; int velocity; };
        struct Pending { Held held; int pitch; };
        struct Channel {
            std::array<std::deque<Held>, 128> held;
            std::vector<Pending> pending;
            bool sustain = false;
        };
        // Keep the many queue objects off the worker's stack.
        auto channels = std::make_unique<std::array<Channel, 16>>();
        std::size_t warnings = 0;
        bool full = false;
        const auto close = [&](Held held, int pitch, int channel, double end) {
            if (!(end > held.start)) { return; }
            if (notes.size() == MidiNotes::maximumNotes) { full = true; return; }
            while (used.contains(nextId)) {
                if (nextId == std::numeric_limits<std::uint64_t>::max()) { full = true; return; }
                ++nextId;
            }
            notes.push_back({nextId, held.start, end - held.start, pitch, held.velocity, channel + 1});
            used.insert(nextId);
        };
        const auto releasePending = [&](Channel& channel, int index, double beat) {
            for (const auto& pending : channel.pending) { close(pending.held, pending.pitch, index, beat); }
            channel.pending.clear();
        };
        const auto release = [&](Channel& channel, int index, int pitch, double beat) {
            auto& queue = channel.held[static_cast<std::size_t>(pitch)];
            if (queue.empty()) { return; }
            const auto held = queue.front(); queue.pop_front();
            if (channel.sustain) { channel.pending.push_back({held, pitch}); }
            else { close(held, pitch, index, beat); }
        };
        for (const auto& event : take.events) {
            if (cancelled(cancel)) { return {nullptr, 0, 0, "MIDI conversion cancelled."}; }
            const int index = event.bytes[0] & 15, kind = event.bytes[0] & 0xf0;
            if (take.config.channel != 0 && index + 1 != take.config.channel) { continue; }
            const int key = event.bytes[1], value = event.bytes[2];
            auto& channel = (*channels)[static_cast<std::size_t>(index)];
            const auto beat = beatAt(event.sample);
            if (kind == 0x90 && value != 0) { channel.held[static_cast<std::size_t>(key)].push_back({beat, value}); }
            else if (kind == 0x80 || kind == 0x90) { release(channel, index, key, beat); }
            else if (kind == 0xb0 && (key == 64 || key == 121)) {
                channel.sustain = key == 64 && value >= 64;
                if (!channel.sustain) { releasePending(channel, index, beat); }
            } else if (kind == 0xb0 && (key == 120 || key == 123)) {
                if (key == 120) {
                    ++warnings; // Notes retain normal envelope release, not a hard cut.
                    releasePending(channel, index, beat);
                }
                const bool sustain = channel.sustain;
                if (key == 120) { channel.sustain = false; }
                for (int pitch = 0; pitch < 128; ++pitch) {
                    while (!channel.held[static_cast<std::size_t>(pitch)].empty()) { release(channel, index, pitch, beat); }
                }
                channel.sustain = sustain;
            } else if (kind == 0xe0) {
                controls.push_back({beat, index + 1, MidiControl::pitchBend, (value << 7 | key) - 8192});
            } else if (kind == 0xb0) {
                controls.push_back({beat, index + 1, key, value});
            } else { ++warnings; }
            if (controls.size() > MidiNotes::maximumControls) { full = true; }
            if (full) { return {nullptr, 0, warnings, "MIDI content exceeds the note or identity limit."}; }
        }
        const auto end = beatAt(take.endSample);
        for (int index = 0; index < 16; ++index) {
            auto& channel = (*channels)[static_cast<std::size_t>(index)];
            channel.sustain = false;
            releasePending(channel, index, end);
            for (int pitch = 0; pitch < 128; ++pitch) {
                while (!channel.held[static_cast<std::size_t>(pitch)].empty()) {
                    if (cancelled(cancel)) { return {nullptr, 0, 0, "MIDI conversion cancelled."}; }
                    release(channel, index, pitch, end);
                }
            }
        }
        if (full) { return {nullptr, 0, warnings, "MIDI content exceeds the note or identity limit."}; }
        const auto added = notes.size() - originalCount;
        auto result = MidiNotes::create(std::move(notes), std::move(controls));
        if (cancelled(cancel)) { return {nullptr, 0, 0, "MIDI conversion cancelled."}; }
        return {std::move(result.source), added, warnings, std::move(result.error)};
    } catch (const std::bad_alloc&) {
        return {nullptr, 0, 0, "Not enough memory to convert MIDI notes."};
    }
};
}
