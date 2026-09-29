#pragma once

#include "../model/Timeline.h"
#include <array>
#include <atomic>

namespace motion {
// Immutable per-clip note ownership. Build on the document/worker thread for
// the destination sample rate; query the same schedule during playback/export.
class PreparedMidiSchedule {
public:
    static constexpr std::size_t maximumVoices = 32;
    struct Voice {
        Id id;
        int pitch, velocity, channel;
        std::int64_t on, off, first, end;
        std::uint64_t age(std::int64_t sample) const {
            if (sample <= on) { return 0; }
            return static_cast<std::uint64_t>(sample) - static_cast<std::uint64_t>(on);
        }
        std::uint64_t heldSamples() const { return static_cast<std::uint64_t>(off - on); }
    };
    struct Result {
        std::shared_ptr<const PreparedMidiSchedule> schedule;
        std::string error;
        explicit operator bool() const { return schedule != nullptr; }
    };

    static Result prepare(const MidiNotes& notes, const Clip& clip, const Tempo& tempo, double sampleRate, std::uint64_t releaseSamples, const std::atomic<bool>* cancel = nullptr, const ClipTiming* resolvedTiming = nullptr) try {
        if (!tempo.valid()
            || !std::isfinite(sampleRate) || sampleRate < 1 || sampleRate > 768000
            || !clip.valid() || !clip.timing(tempo).valid()
            || releaseSamples == 0 || releaseSamples > std::ceil(30 * sampleRate) + 1) {
            return {nullptr, "Invalid MIDI schedule timing or envelope release."};
        }
        const auto cancelled = [&] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
        if (cancelled()) { return {nullptr, "MIDI preparation cancelled."}; }
        const auto timing = resolvedTiming != nullptr ? *resolvedTiming : clip.timing(tempo);
        if (!timing.valid()) { return {nullptr, "Invalid resolved MIDI timing."}; }
        const auto first = quantize(timing.start, sampleRate), end = quantize(timing.end(), sampleRate);
        if (!first || !end || *end <= *first) { return {nullptr, "MIDI clip must occupy at least one output sample."}; }
        auto result = std::shared_ptr<PreparedMidiSchedule>(new PreparedMidiSchedule());
        result->voices.reserve(notes.notes().size());
        struct Event { std::int64_t sample; std::uint32_t voice; bool on; };
        std::vector<Event> events;
        events.reserve(notes.notes().size() * 2);
        const auto secondsPerBeat = 60 / clip.curveBpm(tempo);
        const auto resolve = [&](double beat) { return timing.start + (beat * secondsPerBeat - timing.offset) / timing.rate; };
        for (const auto& note : notes.notes()) {
            if (cancelled()) { return {nullptr, "MIDI preparation cancelled."}; }
            const auto on = quantize(resolve(note.start), sampleRate), off = quantize(resolve(note.end()), sampleRate);
            if (!on || !off) { return {nullptr, "MIDI note timing exceeds the sample clock."}; }
            // PreparedVoiceEnvelope is Done at releaseSamples - 1. Excluding
            // that sample lets zero-release chords retrigger at full capacity.
            const auto tail = *off + static_cast<std::int64_t>(releaseSamples) - 1;
            if (*on >= *end || (tail <= *first && *on < *first)) { continue; }
            if (*off <= *on) { return {nullptr, "An audible MIDI note is shorter than one output sample."}; }
            const auto audibleFirst = std::max(*on, *first), audibleEnd = std::min(tail, *end);
            if (audibleEnd <= audibleFirst) { continue; }
            const auto index = static_cast<std::uint32_t>(result->voices.size());
            result->voices.push_back({note.id, note.pitch, note.velocity, note.channel, *on, *off, audibleFirst, audibleEnd});
            events.push_back({audibleFirst, index, true});
            events.push_back({audibleEnd, index, false});
        }
        std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            if (a.sample != b.sample) { return a.sample < b.sample; }
            if (a.on != b.on) { return !a.on; }
            return a.voice < b.voice;
        });
        result->boundaries.reserve(events.size());
        std::array<std::uint32_t, maximumVoices> active {};
        std::size_t count = 0;
        for (std::size_t event = 0; event < events.size();) {
            if (cancelled()) { return {nullptr, "MIDI preparation cancelled."}; }
            const auto sample = events[event].sample;
            do {
                const auto& change = events[event++];
                auto begin = active.begin(), finish = begin + static_cast<std::ptrdiff_t>(count);
                auto position = std::lower_bound(begin, finish, change.voice);
                if (change.on) {
                    if (count == maximumVoices) { return {nullptr, "MIDI clip exceeds 32 simultaneous notes including release tails."}; }
                    std::move_backward(position, finish, finish + 1);
                    *position = change.voice;
                    ++count;
                } else {
                    if (position == finish || *position != change.voice) { return {nullptr, "Invalid MIDI ownership boundary."}; }
                    std::move(position + 1, finish, position);
                    --count;
                }
            } while (event < events.size() && events[event].sample == sample);
            result->boundaries.push_back({sample, static_cast<std::uint32_t>(result->indices.size()), static_cast<std::uint8_t>(count)});
            result->indices.insert(result->indices.end(), active.begin(), active.begin() + static_cast<std::ptrdiff_t>(count));
        }
        if (cancelled()) { return {nullptr, "MIDI preparation cancelled."}; }
        return {std::move(result), {}};
    } catch (const std::bad_alloc&) {
        return {nullptr, "Not enough memory to prepare MIDI playback."};
    }

    // O(log(note boundaries)) lookup, at most 32 returned voices, no allocation,
    // ownership changes, locks or mutable event cursor on the audio thread.
    std::span<const std::uint32_t> activeAt(std::int64_t sample) const {
        const auto next = std::upper_bound(boundaries.begin(), boundaries.end(), sample,
            [](auto value, const auto& boundary) { return value < boundary.sample; });
        if (next == boundaries.begin()) { return {}; }
        const auto& boundary = *(next - 1);
        return std::span<const std::uint32_t>(indices).subspan(boundary.offset, boundary.count);
    }
    const Voice& voice(std::uint32_t index) const { return voices[index]; }
    std::size_t voiceCount() const { return voices.size(); }

private:
    // Note-on may precede project zero after a trim/slip. Preserve it so phase
    // and envelope age continue, instead of retriggering at the clip boundary.
    static std::optional<std::int64_t> quantize(double seconds, double sampleRate) {
        const auto value = std::round(seconds * sampleRate);
        constexpr double maximumExactSample = 9007199254740991.0;
        if (!std::isfinite(value) || std::abs(value) > maximumExactSample) { return std::nullopt; }
        return static_cast<std::int64_t>(value);
    }
    struct Boundary { std::int64_t sample; std::uint32_t offset; std::uint8_t count; };
    std::vector<Voice> voices;
    std::vector<Boundary> boundaries;
    std::vector<std::uint32_t> indices;
};
}
