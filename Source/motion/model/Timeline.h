#pragma once

#include "Animation.h"
#include "Effects.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace motion {

using Id = std::uint64_t;

struct Clip {
    Id id = 0;
    Id asset = 0;
    std::string name;
    double start = 0.0;
    double duration = 5.0;
    double offset = 0.0;
    double rate = 1.0;
    std::map<std::string, Curve> properties;
    std::vector<EffectInstance> effects;

    double end() const { return start + duration; }
    bool contains(double projectTime) const { return projectTime >= start && projectTime < end(); }
    double localTime(double projectTime) const { return offset + (projectTime - start) * rate; }
    bool valid() const {
        for (const auto& [name, curve] : properties) {
            if (!curve.valid()) {
                return false;
            }
        }
        return id != 0 && std::isfinite(start) && start >= 0.0 && std::isfinite(duration)
            && duration > 0.0 && std::isfinite(end()) && std::isfinite(offset)
            && std::isfinite(rate) && rate > 0.0;
    }

    // Model mutations run on the editor thread. The renderer receives prepared
    // snapshots, never these growing containers.
    bool trim(double newStart, double newEnd) {
        if (!std::isfinite(newStart) || !std::isfinite(newEnd) || newStart < 0.0 || newEnd <= newStart) {
            return false;
        }
        const auto newOffset = localTime(newStart);
        if (!std::isfinite(newOffset)) {
            return false;
        }
        offset = newOffset;
        start = newStart;
        duration = newEnd - newStart;
        return true;
    }

    bool stretch(double newDuration) {
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

    std::optional<std::pair<Clip, Clip>> split(double projectTime, Id rightId) const {
        if (!valid() || rightId == 0 || rightId == id || !std::isfinite(projectTime)
            || projectTime <= start || projectTime >= end()) {
            return std::nullopt;
        }
        auto left = *this;
        auto right = *this;
        left.duration = projectTime - start;
        right.id = rightId;
        right.offset = localTime(projectTime);
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
    bool canPlace(const Clip& candidate, Id replacing = 0) const {
        if (!candidate.valid()) {
            return false;
        }
        for (const auto& clip : clips) {
            if (clip.id != replacing && (clip.id == candidate.id
                || (candidate.start < clip.end() && clip.start < candidate.end()))) {
                return false;
            }
        }
        return true;
    }

    bool insert(Clip clip) {
        if (!canPlace(clip)) {
            return false;
        }
        const auto position = std::lower_bound(clips.begin(), clips.end(), clip.start,
            [](const Clip& existing, double time) { return existing.start < time; });
        clips.insert(position, std::move(clip));
        return true;
    }

    const Clip* at(double projectTime) const {
        const auto next = std::upper_bound(clips.begin(), clips.end(), projectTime,
            [](double time, const Clip& clip) { return time < clip.start; });
        if (next == clips.begin()) {
            return nullptr;
        }
        const auto& clip = *(next - 1);
        return clip.contains(projectTime) ? &clip : nullptr;
    }
};

}
