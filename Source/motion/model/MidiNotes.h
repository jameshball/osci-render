#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>
#include <limits>

namespace motion {
// MIDI content uses quarter-note beats. Clip placement and the project's tempo
// convert these to seconds; changing tempo never rewrites the musical pattern.
struct MidiNote {
    std::uint64_t id = 0;
    double start = 0, duration = 1;
    int pitch = 60, velocity = 100, channel = 1;
    bool operator==(const MidiNote&) const = default;
    double end() const { return start + duration; }
    bool valid() const {
        return id != 0 && std::isfinite(start) && start >= 0 && std::isfinite(duration) && duration > 0
            && std::isfinite(end()) && end() > start && end() <= 1000000
            && pitch >= 0 && pitch <= 127 && velocity >= 1 && velocity <= 127 && channel >= 1 && channel <= 16;
    }
};

// A continuous controller (number 0-127, value 0-127) or pitch bend (number
// pitchBend, value -8192..8191) change at a beat. Values hold until the next
// change on the same channel and number.
struct MidiControl {
    static constexpr int pitchBend = 128;
    double beat = 0;
    int channel = 1, number = 1, value = 0;
    bool operator==(const MidiControl&) const = default;
    bool valid() const {
        return std::isfinite(beat) && beat >= 0 && beat <= 1000000 && channel >= 1 && channel <= 16 && number >= 0 && number <= pitchBend
            && (number == pitchBend ? value >= -8192 && value <= 8191 : value >= 0 && value <= 127);
    }
    // Controllers 0..1; pitch bend -1..1.
    double normalised() const { return number == pitchBend ? value / 8192.0 : value / 127.0; }
};

struct MidiNoteEvent {
    double beat = 0;
    std::uint64_t note = 0;
    int pitch = 60, velocity = 0, channel = 1;
    bool on = false;
};

// Prepared immutable note content. Construction/editing methods allocate and
// belong on the document/worker thread. Event range queries are allocation-free
// binary searches suitable for the shared synth's sample-accurate scheduler.
class MidiNotes {
public:
    static constexpr std::size_t maximumNotes = 100000;
    static constexpr std::size_t maximumControls = 400000;
    struct Result {
        std::shared_ptr<const MidiNotes> source;
        std::string error;
        explicit operator bool() const { return source != nullptr; }
    };
    static Result create(std::vector<MidiNote> notes, std::vector<MidiControl> controls = {}) {
        if (notes.size() > maximumNotes) { return {nullptr, "MIDI content exceeds 100000 notes."}; }
        if (controls.size() > maximumControls) { return {nullptr, "MIDI content exceeds 400000 controller changes."}; }
        for (const auto& control : controls) {
            if (!control.valid()) { return {nullptr, "Controller changes need a finite beat, a channel 1-16 and an in-range value."}; }
        }
        try {
            // Import/build vectors may grow geometrically. Retain only bounded
            // content, not an arbitrary caller-provided spare capacity.
            if (notes.capacity() > maximumNotes) {
                std::vector<MidiNote> bounded(notes.begin(), notes.end());
                notes.swap(bounded);
            }
            std::unordered_set<std::uint64_t> ids;
            ids.reserve(notes.size());
            for (const auto& note : notes) {
                if (!note.valid()) { return {nullptr, "Notes need unique IDs, finite positive beat durations, valid MIDI pitch/velocity/channel, and an end within one million beats."}; }
                if (!ids.insert(note.id).second) { return {nullptr, "MIDI note IDs must be unique within a pattern."}; }
            }
            std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
                return a.start != b.start ? a.start < b.start : a.id < b.id;
            });
            auto result = std::shared_ptr<MidiNotes>(new MidiNotes());
            result->noteData = std::move(notes);
            std::stable_sort(controls.begin(), controls.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
            result->controlData = std::move(controls);
            result->indexControls();
            result->eventData.reserve(result->noteData.size() * 2);
            for (const auto& note : result->noteData) {
                result->lengthBeats = std::max(result->lengthBeats, note.end());
                result->eventData.push_back({note.start, note.id, note.pitch, note.velocity, note.channel, true});
                result->eventData.push_back({note.end(), note.id, note.pitch, 0, note.channel, false});
            }
            std::sort(result->eventData.begin(), result->eventData.end(), [](const auto& a, const auto& b) {
                if (a.beat != b.beat) { return a.beat < b.beat; }
                // Release before retrigger at the same beat. Note identities
                // retain overlapping same-pitch voices for the prepared engine.
                if (a.on != b.on) { return !a.on; }
                return a.note < b.note;
            });
            return {std::move(result), {}};
        } catch (const std::bad_alloc&) {
            return {nullptr, "Not enough memory to prepare MIDI notes."};
        }
    }

    const std::vector<MidiNote>& notes() const { return noteData; }
    const std::vector<MidiControl>& controls() const { return controlData; }
    bool sameContent(const MidiNotes& other) const { return noteData == other.noteData && controlData == other.controlData; }
    bool hasControl(int number, int channel) const {
        const auto key = controlKey(number, channel);
        return key < controlOffsets.size() - 1 && controlOffsets[key + 1] > controlOffsets[key];
    }
    // The value in effect at `beat` (the latest change at or before it), or
    // `fallback` before the first change. Channel 0 means any channel.
    // Allocation-free binary search, safe on the audio thread.
    double controlAt(int number, int channel, double beat, double fallback) const {
        const auto key = controlKey(number, channel);
        if (key + 1 >= controlOffsets.size()) { return fallback; }
        const auto first = controlIndex.begin() + controlOffsets[key], last = controlIndex.begin() + controlOffsets[key + 1];
        const auto after = std::upper_bound(first, last, beat, [this](double value, std::uint32_t index) { return value < controlData[index].beat; });
        return after == first ? fallback : controlData[*(after - 1)].normalised();
    }
    const std::vector<MidiNoteEvent>& events() const { return eventData; }
    double length() const { return lengthBeats; }

    // Half-open intervals avoid duplicate events at neighbouring block edges.
    std::span<const MidiNoteEvent> eventsInRange(double firstBeat, double endBeat) const {
        if (!std::isfinite(firstBeat) || !std::isfinite(endBeat) || endBeat <= firstBeat) { return {}; }
        const auto at = [&](double beat) {
            return std::lower_bound(eventData.begin(), eventData.end(), beat,
                [](const MidiNoteEvent& event, double value) { return event.beat < value; });
        };
        const auto first = at(firstBeat), end = at(endBeat);
        return std::span<const MidiNoteEvent>(eventData).subspan(static_cast<std::size_t>(first - eventData.begin()), static_cast<std::size_t>(end - first));
    }

    // A note edit creates new content for its clip; sibling instances can keep
    // the old shared pattern. Rejected group edits never partially move notes.
    Result withNote(MidiNote note) const try {
        auto next = noteData;
        const auto found = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == note.id; });
        if (found == next.end()) { next.push_back(note); } else { *found = note; }
        return create(std::move(next), controlData);
    } catch (const std::bad_alloc&) {
        return {nullptr, "Not enough memory to edit MIDI notes."};
    }
    Result withoutNotes(std::span<const std::uint64_t> selection) const try {
        const auto selected = selectedIds(selection);
        if (!selected.error.empty()) { return {nullptr, selected.error}; }
        std::vector<MidiNote> next;
        next.reserve(noteData.size() - selected.ids.size());
        for (const auto& note : noteData) {
            if (!selected.ids.contains(note.id)) { next.push_back(note); }
        }
        return create(std::move(next), controlData);
    } catch (const std::bad_alloc&) {
        return {nullptr, "Not enough memory to edit MIDI notes."};
    }
    Result moveNotes(std::span<const std::uint64_t> selection, double beatOffset, int pitchOffset) const try {
        if (!std::isfinite(beatOffset) || pitchOffset < -127 || pitchOffset > 127) { return {nullptr, "Invalid MIDI note movement."}; }
        const auto selected = selectedIds(selection);
        if (!selected.error.empty()) { return {nullptr, selected.error}; }
        auto next = noteData;
        for (auto& note : next) {
            if (selected.ids.contains(note.id)) {
                note.start += beatOffset;
                note.pitch += pitchOffset;
            }
        }
        return create(std::move(next), controlData);
    } catch (const std::bad_alloc&) {
        return {nullptr, "Not enough memory to edit MIDI notes."};
    }
private:
    struct Selection { std::unordered_set<std::uint64_t> ids; std::string error; };
    Selection selectedIds(std::span<const std::uint64_t> selection) const {
        if (selection.size() > noteData.size()) { return {{}, "Invalid MIDI note selection."}; }
        Selection result;
        result.ids.reserve(selection.size());
        for (const auto id : selection) {
            if (!result.ids.insert(id).second) { return {{}, "A note was selected more than once."}; }
        }
        std::size_t found = 0;
        for (const auto& note : noteData) { found += result.ids.contains(note.id) ? 1 : 0; }
        if (found != result.ids.size()) { return {{}, "A selected MIDI note no longer exists."}; }
        return result;
    }
    MidiNotes() = default;
    // One bucket per (number, channel 0-16), channel 0 collecting every channel.
    static std::size_t controlKey(int number, int channel) {
        return number < 0 || number > MidiControl::pitchBend || channel < 0 || channel > 16 ? std::numeric_limits<std::size_t>::max() - 1
            : static_cast<std::size_t>(number) * 17 + static_cast<std::size_t>(channel);
    }
    void indexControls() {
        constexpr std::size_t buckets = (MidiControl::pitchBend + 1) * 17;
        std::vector<std::uint32_t> counts(buckets, 0);
        for (const auto& control : controlData) {
            ++counts[controlKey(control.number, control.channel)];
            ++counts[controlKey(control.number, 0)];
        }
        controlOffsets.assign(buckets + 1, 0);
        for (std::size_t key = 0; key < buckets; ++key) { controlOffsets[key + 1] = controlOffsets[key] + counts[key]; }
        controlIndex.assign(controlOffsets.back(), 0);
        auto cursor = controlOffsets;
        for (std::uint32_t index = 0; index < controlData.size(); ++index) {
            const auto& control = controlData[index];
            controlIndex[cursor[controlKey(control.number, control.channel)]++] = index;
            controlIndex[cursor[controlKey(control.number, 0)]++] = index;
        }
    }
    std::vector<MidiControl> controlData;
    std::vector<std::uint32_t> controlOffsets, controlIndex;
    std::vector<MidiNote> noteData;
    std::vector<MidiNoteEvent> eventData;
    double lengthBeats = 0;
};
}
