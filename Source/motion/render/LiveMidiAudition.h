#pragma once

#include "CompositionRenderer.h"
#include "LiveMidiPerformance.h"
#include "LiveMidiInputs.h"

namespace motion {
inline osci::Point sampleLiveMidiAudition(const PreparedComposition& composition, const PreparedClip& clip, const LiveMidiPerformance& performance, double time, std::uint64_t clock, double rate, bool advancing = false, const LiveSourceFrames* liveFrames = nullptr) {
    if (!std::isfinite(time) || !std::isfinite(rate) || rate <= 0) { return {0, 0, 0, 0, 0, 0}; }
    // Voices share the beam's cycle, as in playback and export.
    const auto phaseAt = [rate, cycleRate = composition.beamRate](std::uint64_t index) { return std::fmod(static_cast<double>(index) * cycleRate / rate, 1.0); };
    const auto current = performance.select(clock, phaseAt(clock));
    if (current.note == 0) { return {0, 0, 0, 0, 0, 0}; }
    const auto previous = clock == 0 ? MidiSelection{} : performance.select(clock - 1, phaseAt(clock - 1));
    const auto next = clock == std::numeric_limits<std::uint64_t>::max() ? MidiSelection{}
        : performance.select(clock + 1, phaseAt(clock + 1));
    // A clip can be auditioned outside its timeline interval. Its nearest
    // content position supplies geometry/transforms while note age stays live.
    const auto position = clip.clampToContent(time);
    auto point = composition.projectPoint(clip.sample(position, current.phase, current.phaseSpan, advancing ? 1 / rate : 0, liveFrames), position);
    bool blank = previous.note != current.note || next.note != current.note;
    if (advancing) {
        const auto frame = std::round(time * rate);
        const auto before = clip.clampToContent((frame - 1) / rate);
        const auto after = clip.clampToContent((frame + 1) / rate);
        blank = blank || composition.activeCamera(before) != composition.activeCamera(position)
            || composition.activeCamera(after) != composition.activeCamera(position);
        const auto* source = clip.resolveSource(liveFrames);
        if (source != nullptr) {
            const auto sourceFrame = source->frameIndex(clip.localTime(position));
            blank = blank || source->frameIndex(clip.localTime(before)) != sourceFrame
                || source->frameIndex(clip.localTime(after)) != sourceFrame;
        }
    }
    if (blank) { point.r = point.g = point.b = 0; }
    return point;
}
}
