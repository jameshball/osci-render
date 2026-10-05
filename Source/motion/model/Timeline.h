#pragma once

#include "Animation.h"
#include "Effects.h"
#include "MidiNotes.h"
#include "MidiInstrument.h"
#include "ClipTiming.h"
#include "Tempo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace motion {
// Intervals are stored as start + duration. Returns the largest duration whose
// floating end does not pass `end`, so adjacent intervals never overlap.
inline double spanUntil(double start, double end) {
    auto duration = end - start;
    while (duration > 0 && start + duration > end) { duration = std::nextafter(duration, 0.0); }
    return duration;
}

using Id = std::uint64_t;

enum class ClipTimeBase { seconds, beats };

struct LuaClipBake;

struct Clip {
    Id id = 0;
    Id asset = 0;
    Id composition = 0;
    std::string name;
    double start = 0.0;
    double duration = 5.0;
    double offset = 0.0;
    double rate = 1.0;
    PropertyMap properties;
    std::vector<EffectInstance> effects;
    // Optional performance for this visual source. Immutable patterns are shared
    // by duplication/undo; an edit replaces only the selected clip's pattern.
    MidiInstrument instrument;
    Id midiAsset = 0;
    std::shared_ptr<const MidiNotes> midi;

    ClipTimeBase timeBase = ClipTimeBase::seconds;
    double contentBpm = 120;
    // Position keys aligned on all three axes travel one spatial path; aligned
    // rotation keys interpolate as orientations, free of gimbal lock.
    bool spatialPath = false;
    bool quaternionRotation = false;
    // Lua clips with sliders keep their own baked frames (runtime cache, also
    // saved with the project).
    std::shared_ptr<const LuaClipBake> luaBake;

    // Canonical fields above use beats for musical clips, seconds otherwise.
    // Resolved content remains seconds so visual curves and their tangents are
    // never rewritten when the project tempo changes. Under a tempo map a
    // musical clip's content follows the beats exactly, through steps and
    // ramps alike.
    ClipTiming timing(const Tempo& tempo) const {
        if (timeBase == ClipTimeBase::seconds) { return {start, end(), offset, rate}; }
        const auto first = tempo.seconds(start);
        const auto last = tempo.seconds(end());
        ClipTiming resolved(first, last, offset * (60 / contentBpm), rate * (tempo.averageBpm(start, end()) / contentBpm));
        if (!tempo.constant()) { resolved.warp = ClipTiming::BeatWarp {tempo, 0, 1, start, rate * 60 / contentBpm}; }
        return resolved;
    }
    double curveBpm(const Tempo& tempo) const { return timeBase == ClipTimeBase::beats ? contentBpm : tempo.initialBpm(); }
    bool setTiming(ClipTiming value, const Tempo& tempo) {
        if (!tempo.valid() || !value.valid()) { return false; }
        auto next = *this;
        const auto before = timing(tempo);
        if (timeBase == ClipTimeBase::beats) {
            // Unchanged fields keep their exact canonical values. A pure move
            // keeps its length and content speed in beats, like its bars.
            const bool moved = value.start != before.start;
            if (moved) { next.start = tempo.beats(value.start); }
            if (value.duration() != before.duration()) {
                next.duration = tempo.constant() ? value.duration() * (tempo.initialBpm() / 60) : tempo.beats(value.end()) - next.start;
            }
            if (value.offset != before.offset) { next.offset = value.offset * (contentBpm / 60); }
            if (value.rate != before.rate) {
                next.rate = value.rate * (contentBpm / tempo.averageBpm(next.start, next.end()));
            }
        } else {
            next.start = value.start; next.duration = value.duration(); next.offset = value.offset; next.rate = value.rate;
        }
        if (!next.valid() || !next.timing(tempo).valid()) { return false; }
        start = next.start; duration = next.duration; offset = next.offset; rate = next.rate;
        return true;
    }
    bool anchorToBeats(const Tempo& tempo) {
        if (!tempo.valid() || !valid()) { return false; }
        if (timeBase == ClipTimeBase::beats) { return true; }
        const auto before = timing(tempo);
        if (!before.valid()) { return false; }
        auto next = *this;
        next.timeBase = ClipTimeBase::beats;
        next.start = tempo.beats(before.start);
        next.duration = tempo.beats(before.end()) - next.start;
        // Content keeps its seconds: author it at the placement's own tempo.
        next.contentBpm = tempo.averageBpm(next.start, next.end());
        next.offset = before.offset * (next.contentBpm / 60);
        // Reciprocal conversion can expand a touching interval by one ULP.
        // Move only inward, and check the actual start + duration endpoint:
        // accepting an overlap tolerance would also permit real overlaps.
        for (int step = 0; step < 4 && tempo.seconds(next.start) < before.start; ++step) {
            next.start = std::nextafter(next.start, std::numeric_limits<double>::infinity());
        }
        for (int step = 0; step < 4 && tempo.seconds(next.end()) > before.end(); ++step) {
            const auto inwardEnd = std::nextafter(next.end(), 0.0);
            next.duration = inwardEnd - next.start;
        }
        if (!next.valid() || !next.timing(tempo).valid()) { return false; }
        const auto resolved = next.timing(tempo);
        if (resolved.start < before.start || resolved.end() > before.end()) { return false; }
        *this = std::move(next);
        return true;
    }
    double end() const { return start + duration; }
    bool contains(double projectTime, const Tempo& tempo) const { const auto t = timing(tempo); return projectTime >= t.start && projectTime < t.end(); }
    double localTime(double projectTime, const Tempo& tempo) const { return timing(tempo).localTime(projectTime); }
    bool valid() const {
        if (!instrument.valid()) { return false; }
        for (const auto& [name, curve] : properties) {
            if (!curve.valid()) {
                return false;
            }
        }
        return (timeBase == ClipTimeBase::seconds || timeBase == ClipTimeBase::beats)
            && std::isfinite(contentBpm) && contentBpm >= 1 && contentBpm <= 1000
            && !(asset != 0 && composition != 0)
            && id != 0 && std::isfinite(start) && start >= 0.0 && std::isfinite(duration)
            && duration > 0.0 && std::isfinite(end()) && end() > start && std::isfinite(offset)
            && std::isfinite(rate) && rate > 0.0;
    }

    // Model mutations run on the editor thread. The renderer receives prepared
    // snapshots, never these growing containers.
    bool trim(double newStart, double newEnd, const Tempo& tempo) {
        if (!tempo.valid()) { return false; }
        if (timeBase == ClipTimeBase::beats) { newStart = tempo.beats(newStart); newEnd = tempo.beats(newEnd); }
        if (!std::isfinite(newStart) || !std::isfinite(newEnd) || newStart < 0.0 || newEnd <= newStart) {
            return false;
        }
        const auto newOffset = offset + (newStart - start) * rate;
        if (!std::isfinite(newOffset)) {
            return false;
        }
        offset = newOffset;
        start = newStart;
        duration = newEnd - newStart;
        return true;
    }

    bool stretch(double newDuration, const Tempo& tempo) {
        if (!tempo.valid()) { return false; }
        if (timeBase == ClipTimeBase::beats) { newDuration = tempo.beats(tempo.seconds(start) + newDuration) - start; }
        if (!std::isfinite(newDuration) || newDuration <= 0.0) {
            return false;
        }
        const auto newRate = rate * duration / newDuration;
        if (!std::isfinite(newRate) || newRate <= 0.0 || !std::isfinite(start + newDuration)) {
            return false;
        }
        rate = newRate;
        duration = newDuration;
        return true;
    }

    // Both halves end exactly where the original boundaries were: neither can
    // round past the split point or the original end into a neighbour.
    std::optional<std::pair<Clip, Clip>> split(double projectTime, Id rightId, const Tempo& tempo) const {
        if (!tempo.valid()) { return std::nullopt; }
        if (timeBase == ClipTimeBase::beats) { projectTime = tempo.beats(projectTime); }
        if (!valid() || rightId == 0 || rightId == id || !std::isfinite(projectTime)
            || projectTime <= start || projectTime >= end()) {
            return std::nullopt;
        }
        auto left = *this;
        auto right = *this;
        left.duration = spanUntil(start, projectTime);
        right.id = rightId;
        right.offset = offset + (projectTime - start) * rate;
        right.start = projectTime;
        right.duration = spanUntil(projectTime, end());
        if (!(left.duration > 0) || !(right.duration > 0)) { return std::nullopt; }
        return std::make_pair(std::move(left), std::move(right));
    }
};

enum class TrackKind { visual, audio };

struct Track {
    Id id = 0;
    std::string name;
    std::vector<Clip> clips;
    std::vector<EffectInstance> effects;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    Id group = 0;
    TrackKind kind = TrackKind::visual;
    // Live MIDI input: 0 off, 1-16 one channel, 17 any channel. Armed visual
    // tracks play and record what arrives, drawn inside the composition.
    int midiInput = 0;
    // Timeline row height in pixels (0: the default). View state: saved with
    // the project, changed without undo and kept across undo/redo.
    int height = 0;
    static constexpr int minimumHeight = 22, maximumHeight = 240;
    // Label colour index for the timeline (0: automatic, by clip kind).
    int label = 0;
    static constexpr int anyMidiChannel = 17;

    // Overlap requires an explicit transition (added by the transition model).
    // Ordinary placement is non-destructive: rejection leaves existing clips intact.
    bool canPlace(const Clip& candidate, Id replacing, const Tempo& tempo) const {
        if (!candidate.valid() || !tempo.valid()) {
            return false;
        }
        const auto proposed = candidate.timing(tempo);
        if (!proposed.valid()) { return false; }
        for (const auto& clip : clips) {
            const auto existing = clip.timing(tempo);
            if (clip.id != replacing && (clip.id == candidate.id
                || (proposed.start < existing.end() && existing.start < proposed.end()))) {
                return false;
            }
        }
        return true;
    }

    bool insert(Clip clip, const Tempo& tempo) {
        if (!canPlace(clip, 0, tempo)) {
            return false;
        }
        const auto position = std::lower_bound(clips.begin(), clips.end(), clip.timing(tempo).start,
            [&tempo](const Clip& existing, double time) { return existing.timing(tempo).start < time; });
        clips.insert(position, std::move(clip));
        return true;
    }

    const Clip* at(double projectTime, const Tempo& tempo) const {
        // Tempo can change the ordering of mixed beat/seconds clips.
        for (const auto& clip : clips) {
            if (clip.contains(projectTime, tempo)) { return &clip; }
        }
        return nullptr;
    }
};

// Editor-thread operation: trim one clip and ripple only clips after its
// original end. All displacement is resolved project seconds, so musical and
// time-anchored clips retain their own authoring domains and source clocks.
inline bool rippleTrim(Track& track, Id clipId, bool leadingEdge, double deltaSeconds, const Tempo& bpm) {
    if (track.locked || clipId == 0 || !std::isfinite(deltaSeconds) || !bpm.valid()) {
        return false;
    }
    auto candidate = track;
    std::vector<Id> identities;
    identities.reserve(candidate.clips.size());
    for (const auto& clip : candidate.clips) {
        if (!clip.valid() || !clip.timing(bpm).valid()) {
            return false;
        }
        identities.push_back(clip.id);
    }
    std::sort(identities.begin(), identities.end());
    if (std::adjacent_find(identities.begin(), identities.end()) != identities.end()) {
        return false;
    }
    std::sort(candidate.clips.begin(), candidate.clips.end(), [&bpm](const auto& left, const auto& right) {
        return left.timing(bpm).start < right.timing(bpm).start;
    });
    double previousEnd = 0;
    ClipTiming original;
    std::size_t selected = candidate.clips.size();
    for (std::size_t index = 0; index < candidate.clips.size(); ++index) {
        const auto& clip = candidate.clips[index];
        const auto timing = clip.timing(bpm);
        if (timing.start < previousEnd) {
            return false;
        }
        if (clip.id == clipId) {
            if (selected != candidate.clips.size()) {
                return false;
            }
            selected = index;
            original = timing;
        }
        previousEnd = timing.end();
    }
    if (selected == candidate.clips.size()) {
        return false;
    }
    auto edited = original;
    if (leadingEdge) {
        edited.setDuration(original.duration() - deltaSeconds);
        edited.offset = original.offset + deltaSeconds * original.rate;
    } else {
        edited.setDuration(original.duration() + deltaSeconds);
    }
    if (!edited.valid() || !candidate.clips[selected].setTiming(edited, bpm)) {
        return false;
    }
    const auto displacement = leadingEdge ? -deltaSeconds : deltaSeconds;
    for (std::size_t index = selected + 1; index < candidate.clips.size(); ++index) {
        auto timing = candidate.clips[index].timing(bpm);
        timing.moveTo(timing.start + displacement);
        if (!timing.valid() || !candidate.clips[index].setTiming(timing, bpm)) {
            return false;
        }
    }
    for (std::size_t index = 1; index < candidate.clips.size(); ++index) {
        auto& clip = candidate.clips[index];
        const auto previous = candidate.clips[index - 1].timing(bpm);
        auto timing = clip.timing(bpm);
        if (timing.start >= previous.end()) {
            continue;
        }
        const auto overlap = previous.end() - timing.start;
        const auto tolerance = 32 * std::numeric_limits<double>::epsilon()
            * std::max({1.0, std::abs(timing.start), std::abs(previous.end()), std::abs(displacement)});
        if (overlap > tolerance) {
            return false;
        }
        timing.moveTo(previous.end());
        if (!clip.setTiming(timing, bpm)) {
            return false;
        }
        for (int step = 0; step < 4 && clip.timing(bpm).start < previous.end(); ++step) {
            clip.start = std::nextafter(clip.start, std::numeric_limits<double>::infinity());
        }
        if (!clip.valid() || clip.timing(bpm).start < previous.end()) {
            return false;
        }
    }
    track = std::move(candidate);
    return true;
}

// Editor-thread operation: apply a shared project-time displacement atomically.
// Track displacement is in model rows; callers with collapsed groups translate
// their visible-row gesture before invoking this operation.
inline bool moveClips(std::vector<Track>& tracks, const std::vector<Id>& ids, double seconds, int trackDelta, const Tempo& bpm) {
    if (ids.empty() || !std::isfinite(seconds) || !bpm.valid()) { return false; }
    auto unique = ids;
    std::sort(unique.begin(), unique.end());
    if (unique.front() == 0 || std::adjacent_find(unique.begin(), unique.end()) != unique.end()) { return false; }
    struct Placement { std::size_t row; Clip clip; };
    std::vector<Placement> moving;
    moving.reserve(ids.size());
    for (std::size_t row = 0; row < tracks.size(); ++row) {
        for (const auto& clip : tracks[row].clips) {
            if (!std::binary_search(unique.begin(), unique.end(), clip.id)) { continue; }
            const auto destination = static_cast<std::int64_t>(row) + trackDelta;
            if (tracks[row].locked || destination < 0 || destination >= static_cast<std::int64_t>(tracks.size())) { return false; }
            const auto target = static_cast<std::size_t>(destination);
            if (tracks[target].locked || tracks[target].kind != tracks[row].kind) { return false; }
            auto candidate = clip;
            auto timing = candidate.timing(bpm);
            timing.moveTo(timing.start + seconds);
            if (!candidate.setTiming(timing, bpm)) { return false; }
            moving.push_back({target, std::move(candidate)});
        }
    }
    if (moving.size() != ids.size()) { return false; }
    auto updated = tracks;
    for (auto& track : updated) {
        std::erase_if(track.clips, [&](const auto& clip) { return std::binary_search(unique.begin(), unique.end(), clip.id); });
    }
    for (auto& placement : moving) {
        if (!updated[placement.row].insert(std::move(placement.clip), bpm)) { return false; }
    }
    tracks = std::move(updated);
    return true;
}

}
