#pragma once

#include "Animation.h"
#include "Effects.h"
#include "MidiNotes.h"
#include "MidiInstrument.h"
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace motion {

using Id = std::uint64_t;

enum class ClipTimeBase { seconds, beats };

struct ClipTiming {
    ClipTiming(double first = 0, double last = 0, double sourceOffset = 0, double speed = 1)
        : start(first), offset(sourceOffset), rate(speed), finish(last), length(last - first) {}
    double start, offset, rate;
    double end() const { return finish; }
    double duration() const { return length; }
    void moveTo(double value) { start = value; finish = value + length; }
    void setStart(double value) { start = value; length = finish - start; }
    void setEnd(double value) { finish = value; length = finish - start; }
    void setDuration(double value) { length = value; finish = start + value; }
    double localTime(double time) const { return offset + (time - start) * rate; }
    // Resolve this child interval through a composition instance. Both inputs
    // are seconds at their own scope; authored curves remain in source time.
    // Clipping the visible interval also advances the source offset, so a trim
    // cannot restart the child's animation.
    std::optional<ClipTiming> nestedIn(const ClipTiming& instance) const {
        if (!valid() || !instance.valid()) { return std::nullopt; }
        const auto mappedStart = instance.start + (start - instance.offset) / instance.rate;
        const auto mappedEnd = instance.start + (end() - instance.offset) / instance.rate;
        if (!std::isfinite(mappedStart) || !std::isfinite(mappedEnd)) { return std::nullopt; }
        const auto first = std::max(instance.start, mappedStart);
        const auto last = std::min(instance.end(), mappedEnd);
        if (first >= last) { return std::nullopt; }
        ClipTiming resolved(first, last, localTime(instance.localTime(first)), rate * instance.rate);
        return resolved.valid() ? std::optional<ClipTiming>(resolved) : std::nullopt;
    }
    bool valid() const {
        return std::isfinite(start) && start >= 0 && std::isfinite(length) && length > 0
            && std::isfinite(finish) && finish > start && std::isfinite(offset) && std::isfinite(rate) && rate > 0;
    }
private:
    // Keep the converted boundary itself: start + (end - start) can round to
    // a different value and invent overlaps between exactly adjacent clips.
    double finish, length;
};

struct Clip {
    Id id = 0;
    Id asset = 0;
    Id composition = 0;
    std::string name;
    double start = 0.0;
    double duration = 5.0;
    double offset = 0.0;
    double rate = 1.0;
    std::map<std::string, Curve> properties;
    std::vector<EffectInstance> effects;
    // Optional performance for this visual source. Immutable patterns are shared
    // by duplication/undo; an edit replaces only the selected clip's pattern.
    MidiInstrument instrument;
    Id midiAsset = 0;
    std::shared_ptr<const MidiNotes> midi;

    ClipTimeBase timeBase = ClipTimeBase::seconds;
    double contentBpm = 120;

    // Canonical fields above use beats for musical clips, seconds otherwise.
    // Resolved content remains seconds so visual curves and their tangents are
    // never rewritten when the project tempo changes.
    ClipTiming timing(double projectBpm) const {
        if (timeBase == ClipTimeBase::seconds) { return {start, end(), offset, rate}; }
        const auto scale = 60 / projectBpm;
        const auto first = start * scale;
        const auto last = end() * scale;
        return {first, last, offset * (60 / contentBpm), rate * (projectBpm / contentBpm)};
    }
    double curveBpm(double projectBpm) const { return timeBase == ClipTimeBase::beats ? contentBpm : projectBpm; }
    bool setTiming(ClipTiming value, double projectBpm) {
        if (!std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000 || !value.valid()) { return false; }
        auto next = *this;
        const auto before = timing(projectBpm);
        const auto placementScale = timeBase == ClipTimeBase::beats ? projectBpm / 60 : 1;
        const auto contentScale = timeBase == ClipTimeBase::beats ? contentBpm / 60 : 1;
        if (value.start != before.start) { next.start = value.start * placementScale; }
        if (value.duration() != before.duration()) { next.duration = value.duration() * placementScale; }
        if (value.offset != before.offset) { next.offset = value.offset * contentScale; }
        if (value.rate != before.rate) { next.rate = value.rate * (contentScale / placementScale); }
        if (!next.valid() || !next.timing(projectBpm).valid()) { return false; }
        start = next.start; duration = next.duration; offset = next.offset; rate = next.rate;
        return true;
    }
    bool anchorToBeats(double projectBpm) {
        if (!std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000 || !valid()) { return false; }
        if (timeBase == ClipTimeBase::beats) { return true; }
        const auto before = timing(projectBpm);
        if (!before.valid()) { return false; }
        auto next = *this;
        next.timeBase = ClipTimeBase::beats; next.contentBpm = projectBpm;
        next.start = before.start * (projectBpm / 60);
        next.duration = before.duration() * (projectBpm / 60);
        next.offset = before.offset * (projectBpm / 60);
        // Reciprocal conversion can expand a touching interval by one ULP.
        // Move only inward, and check the actual start + duration endpoint:
        // accepting an overlap tolerance would also permit real overlaps.
        const auto secondsPerBeat = 60 / projectBpm;
        for (int step = 0; step < 4 && next.start * secondsPerBeat < before.start; ++step) {
            next.start = std::nextafter(next.start, std::numeric_limits<double>::infinity());
        }
        for (int step = 0; step < 4 && next.end() * secondsPerBeat > before.end(); ++step) {
            const auto inwardEnd = std::nextafter(next.end(), 0.0);
            next.duration = inwardEnd - next.start;
        }
        if (!next.valid() || !next.timing(projectBpm).valid()) { return false; }
        const auto resolved = next.timing(projectBpm);
        if (resolved.start < before.start || resolved.end() > before.end()) { return false; }
        *this = std::move(next);
        return true;
    }
    double end() const { return start + duration; }
    bool contains(double projectTime, double projectBpm = 120) const { const auto t = timing(projectBpm); return projectTime >= t.start && projectTime < t.end(); }
    double localTime(double projectTime, double projectBpm = 120) const { return timing(projectBpm).localTime(projectTime); }
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
    bool trim(double newStart, double newEnd, double projectBpm = 120) {
        if (!std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000) { return false; }
        if (timeBase == ClipTimeBase::beats) { newStart *= projectBpm / 60; newEnd *= projectBpm / 60; }
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

    bool stretch(double newDuration, double projectBpm = 120) {
        if (!std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000) { return false; }
        if (timeBase == ClipTimeBase::beats) { newDuration *= projectBpm / 60; }
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

    std::optional<std::pair<Clip, Clip>> split(double projectTime, Id rightId, double projectBpm = 120) const {
        if (!std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000) { return std::nullopt; }
        if (timeBase == ClipTimeBase::beats) { projectTime *= projectBpm / 60; }
        if (!valid() || rightId == 0 || rightId == id || !std::isfinite(projectTime)
            || projectTime <= start || projectTime >= end()) {
            return std::nullopt;
        }
        auto left = *this;
        auto right = *this;
        left.duration = projectTime - start;
        right.id = rightId;
        right.offset = offset + (projectTime - start) * rate;
        right.start = projectTime;
        right.duration = end() - projectTime;
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

    // Overlap requires an explicit transition (added by the transition model).
    // Ordinary placement is non-destructive: rejection leaves existing clips intact.
    bool canPlace(const Clip& candidate, Id replacing = 0, double projectBpm = 120) const {
        if (!candidate.valid() || !std::isfinite(projectBpm) || projectBpm < 1 || projectBpm > 1000) {
            return false;
        }
        const auto proposed = candidate.timing(projectBpm);
        if (!proposed.valid()) { return false; }
        for (const auto& clip : clips) {
            const auto existing = clip.timing(projectBpm);
            if (clip.id != replacing && (clip.id == candidate.id
                || (proposed.start < existing.end() && existing.start < proposed.end()))) {
                return false;
            }
        }
        return true;
    }

    bool insert(Clip clip, double projectBpm = 120) {
        if (!canPlace(clip, 0, projectBpm)) {
            return false;
        }
        const auto position = std::lower_bound(clips.begin(), clips.end(), clip.timing(projectBpm).start,
            [projectBpm](const Clip& existing, double time) { return existing.timing(projectBpm).start < time; });
        clips.insert(position, std::move(clip));
        return true;
    }

    const Clip* at(double projectTime, double projectBpm = 120) const {
        // Tempo can change the ordering of mixed beat/seconds clips.
        for (const auto& clip : clips) {
            if (clip.contains(projectTime, projectBpm)) { return &clip; }
        }
        return nullptr;
    }
};

// Editor-thread operation: apply a shared project-time displacement atomically.
// Track displacement is in model rows; callers with collapsed groups translate
// their visible-row gesture before invoking this operation.
inline bool moveClips(std::vector<Track>& tracks, const std::vector<Id>& ids, double seconds, int trackDelta, double bpm) {
    if (ids.empty() || !std::isfinite(seconds) || !std::isfinite(bpm) || bpm < 1 || bpm > 1000) { return false; }
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
