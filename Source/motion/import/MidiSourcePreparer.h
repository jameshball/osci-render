#pragma once

#include "../model/MidiNotes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace motion {
// Strict, bounded Standard MIDI File importer. All work is off the audio thread.
// PPQ files preserve quarter-note beats, independent of embedded tempo changes.
// SMPTE files convert their absolute time to beats at the supplied project tempo.
class MidiSourcePreparer {
public:
    static constexpr std::size_t maximumEncodedBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maximumNotes = 100000, maximumEvents = 200000;
    static constexpr double maximumBeats = 1000000;
    struct Result {
        std::shared_ptr<const MidiNotes> source;
        double suggestedBpm = 120;
        int ignoredEvents = 0;
        std::string error;
        explicit operator bool() const { return source != nullptr; }
    };

    // Same-key overlaps pair FIFO within each track/channel/pitch, then tracks
    // flatten as simultaneous voices. Unmatched offs, dangling ons and zero-length
    // notes reject the whole file; no fabricated holds or partial source is returned.
    static Result prepare(const void* data, std::size_t size, double projectBpm, const std::atomic<bool>* cancel = nullptr) {
        try {
            checkCancel(cancel);
            if (data == nullptr || size == 0 || size > maximumEncodedBytes) { throw Error("MIDI file must contain 1 byte to 4 MiB."); }
            if (!std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000) { throw Error("Project tempo must be between 1 and 1000 BPM for MIDI import."); }
            Reader file { static_cast<const std::uint8_t*>(data), size };
            file.tag("MThd");
            const auto headerLength = file.big32();
            if (headerLength < 6) { throw Error("MIDI header must contain at least 6 bytes."); }
            auto header = file.substream(headerLength);
            const auto format = header.big16(), trackCount = header.big16(), division = header.big16();
            if (format == 2) { throw Error("MIDI format 2 contains independent songs. Export it as format 0 or 1 before importing."); }
            if (format > 1 || trackCount == 0 || (format == 0 && trackCount != 1)) { throw Error("MIDI format or track count is invalid."); }
            double beatsPerTick = 0;
            const bool smpte = (division & 0x8000) != 0;
            if (!smpte) {
                if (division == 0) { throw Error("MIDI ticks per quarter note must be positive."); }
                beatsPerTick = 1.0 / division;
            } else {
                const auto code = static_cast<int>(division >> 8) - 256;
                const auto ticksPerFrame = division & 255;
                if (ticksPerFrame == 0 || (code != -24 && code != -25 && code != -29 && code != -30)) { throw Error("MIDI SMPTE division must use 24, 25, 29-drop or 30 FPS with positive ticks per frame."); }
                const auto framesPerSecond = code == -29 ? 30000.0 / 1001.0 : static_cast<double>(-code);
                beatsPerTick = projectBpm / (60.0 * framesPerSecond * ticksPerFrame);
            }
            Result result;
            if (smpte) { result.suggestedBpm = projectBpm; }
            bool initialTempo = false;
            std::size_t events = 0, noteOns = 0;
            std::vector<MidiNote> notes;
            unsigned tracksRead = 0;
            std::size_t chunksRead = 0;
            while (!file.empty()) {
                checkCancel(cancel);
                // Every chunk consumes at least eight encoded bytes, even when
                // empty. Explicitly retain that file-size-derived iteration cap.
                if (++chunksRead > maximumEncodedBytes / 8) { throw Error("MIDI file contains too many chunks."); }
                const auto chunkType = file.substream(4);
                const auto length = file.big32();
                auto track = file.substream(length);
                if (std::memcmp(chunkType.data, "MThd", 4) == 0) { throw Error("MIDI contains a duplicate header chunk."); }
                if (std::memcmp(chunkType.data, "MTrk", 4) != 0) {
                    ++result.ignoredEvents; // Unknown container records are explicitly skipped.
                    continue;
                }
                if (++tracksRead > trackCount) { throw Error("MIDI contains more track chunks than its header declares."); }
                std::array<int, 2048> heads, tails;
                heads.fill(-1); tails.fill(-1);
                std::vector<Pending> pending;
                std::uint64_t ticks = 0;
                unsigned runningStatus = 0;
                bool ended = false;
                while (!track.empty()) {
                    checkCancel(cancel);
                    if (++events > maximumEvents) { throw Error("MIDI file exceeds the 200000-event import limit."); }
                    const auto delta = track.vlq();
                    if (ticks > std::numeric_limits<std::uint64_t>::max() - delta) { throw Error("MIDI event time overflows its tick counter."); }
                    ticks += delta;
                    const auto beat = static_cast<double>(ticks) * beatsPerTick;
                    if (!std::isfinite(beat) || beat > maximumBeats) { throw Error("MIDI event time exceeds one million beats."); }
                    auto status = static_cast<unsigned>(track.peek());
                    if ((status & 0x80) != 0) { track.byte(); }
                    else {
                        if (runningStatus == 0) { throw Error("MIDI running status has no preceding channel message."); }
                        status = runningStatus;
                    }
                    if (status < 0xf0) {
                        runningStatus = status;
                        const auto type = status & 0xf0, channel = (status & 15) + 1;
                        const auto first = track.dataByte();
                        const auto second = type == 0xc0 || type == 0xd0 ? 0U : track.dataByte();
                        const auto key = (channel - 1) * 128 + first;
                        if (type == 0x90 && second != 0) {
                            if (++noteOns > maximumNotes) { throw Error("MIDI file exceeds the 100000-note import limit."); }
                            const auto index = static_cast<int>(pending.size());
                            pending.push_back({ ticks, noteOns, static_cast<int>(first), static_cast<int>(second), static_cast<int>(channel), -1 });
                            if (tails[key] >= 0) { pending[static_cast<std::size_t>(tails[key])].next = index; }
                            else { heads[key] = index; }
                            tails[key] = index;
                        } else if (type == 0x80 || (type == 0x90 && second == 0)) {
                            const auto index = heads[key];
                            if (index < 0) { throw Error("MIDI note-off has no matching note-on in its track and channel."); }
                            const auto& on = pending[static_cast<std::size_t>(index)];
                            if (ticks == on.ticks) { throw Error("MIDI contains a zero-duration note. Remove it or give it a positive length."); }
                            const auto startBeat = static_cast<double>(on.ticks) * beatsPerTick;
                            notes.push_back({ on.id, startBeat, beat - startBeat, on.pitch, on.velocity, on.channel });
                            heads[key] = on.next;
                            if (heads[key] < 0) { tails[key] = -1; }
                        } else { ++result.ignoredEvents; }
                    } else {
                        // SMF meta and SysEx events cancel channel running status.
                        runningStatus = 0;
                        if (status == 0xff) {
                            const auto type = track.dataByte();
                            const auto count = track.vlq();
                            auto payload = track.substream(count);
                            if (type == 0x2f) {
                                if (count != 0 || !track.empty()) { throw Error("MIDI end-of-track must have zero length and terminate its track chunk."); }
                                ended = true; break;
                            }
                            if (type == 0x51) {
                                if (count != 3) { throw Error("MIDI tempo event must contain exactly three bytes."); }
                                const auto high = payload.byte(), middle = payload.byte(), low = payload.byte();
                                const auto tempo = (static_cast<unsigned>(high) << 16) | (static_cast<unsigned>(middle) << 8) | low;
                                if (tempo == 0) { throw Error("MIDI tempo must have a positive microsecond duration."); }
                                if (!smpte && ticks == 0 && !initialTempo) {
                                    result.suggestedBpm = 60000000.0 / tempo;
                                    initialTempo = true;
                                } else { ++result.ignoredEvents; }
                            } else { ++result.ignoredEvents; }
                        } else if (status == 0xf0 || status == 0xf7) {
                            const auto count = track.vlq();
                            track.substream(count);
                            ++result.ignoredEvents;
                        } else { throw Error("MIDI contains an invalid SMF system status byte."); }
                    }
                }
                if (!ended) { throw Error("MIDI track is missing its end-of-track event."); }
                if (std::any_of(heads.begin(), heads.end(), [](int value) { return value >= 0; })) {
                    throw Error("MIDI contains an unfinished note-on. Add its note-off before importing.");
                }
            }
            if (tracksRead != trackCount) { throw Error("MIDI contains fewer track chunks than its header declares."); }
            checkCancel(cancel);
            std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.start != b.start ? a.start < b.start : a.id < b.id; });
            const auto prepared = MidiNotes::create(std::move(notes));
            if (!prepared) { result.error = prepared.error; return result; }
            checkCancel(cancel);
            result.source = prepared.source;
            return result;
        } catch (const std::bad_alloc&) { return { nullptr, 120, 0, "Not enough memory to import MIDI notes." }; }
        catch (const std::exception& error) { return { nullptr, 120, 0, error.what() }; }
        catch (...) { return { nullptr, 120, 0, "MIDI import failed." }; }
    }
private:
    struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
    static void checkCancel(const std::atomic<bool>* cancel) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) { throw Error("MIDI import cancelled."); }
    }
    struct Pending {
        std::uint64_t ticks, id;
        int pitch, velocity, channel, next;
    };
    struct Reader {
        const std::uint8_t* data;
        std::size_t size, position = 0;
        bool empty() const { return position == size; }
        std::uint8_t peek() const { if (empty()) { throw Error("MIDI file is truncated."); } return data[position]; }
        std::uint8_t byte() { const auto value = peek(); ++position; return value; }
        unsigned dataByte() { const auto value = byte(); if (value >= 128) { throw Error("MIDI channel data contains an unexpected status byte."); } return value; }
        unsigned big16() { const auto high = byte(); return (static_cast<unsigned>(high) << 8) | byte(); }
        std::uint32_t big32() { const auto high = big16(); return (static_cast<std::uint32_t>(high) << 16) | big16(); }
        void tag(const char* expected) {
            auto value = substream(4);
            if (std::memcmp(value.data, expected, 4) != 0) { throw Error("MIDI chunk signature is invalid."); }
        }
        Reader substream(std::size_t count) {
            if (count > size - position) { throw Error("MIDI chunk or event payload is truncated."); }
            Reader result { data + position, count }; position += count; return result;
        }
        std::uint32_t vlq() {
            std::uint32_t value = 0;
            for (unsigned index = 0; index < 4; ++index) {
                const auto next = byte();
                if (index == 0 && next == 0x80) { throw Error("MIDI variable-length quantity uses a noncanonical leading zero."); }
                value = (value << 7) | (next & 127);
                if ((next & 128) == 0) { return value; }
            }
            throw Error("MIDI variable-length quantity exceeds four bytes.");
        }
    };
};
}
