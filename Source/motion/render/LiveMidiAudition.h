#pragma once

#include "CompositionRenderer.h"
#include "LiveMidiPerformance.h"
#include "LiveMidiInputs.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace motion {
// Inspect short messages directly: constructing a JUCE MidiMessage for an
// arbitrary incoming SysEx packet can allocate on the audio thread.
inline bool applyLiveMidi(LiveMidiPerformance& performance, const unsigned char* data, int size, std::uint64_t sample) {
    if (data == nullptr || size != 3 || data[1] > 127 || data[2] > 127) { return false; }
    const auto channel = (data[0] & 15) + 1;
    switch (data[0] & 0xf0) {
        case 0x80: return performance.noteOff(channel, data[1], sample);
        case 0x90: return performance.noteOn(channel, data[1], data[2], sample);
        case 0xe0: return performance.pitchBend(channel, (data[2] << 7 | data[1]) - 8192, sample);
        case 0xb0:
            if (data[1] == 11) { return performance.setExpression(channel, data[2]); }
            if (data[1] == 64) { return performance.sustain(channel, data[2] >= 64, sample); }
            if (data[1] == 120) { return performance.allSoundOff(channel); }
            if (data[1] == 123) { return performance.allNotesOff(channel, sample); }
            if (data[1] == 121) { return performance.sustain(channel, false, sample); }
            return false;
        default: return false;
    }
}

// One cursor per block. The buffer must remain unchanged/alive while iterating;
// offsets advance monotonically and absoluteClock already includes the offset.
// Null performance consumes input without enabling or retaining live audition.
class LiveMidiInputCursor {
public:
    explicit LiveMidiInputCursor(const juce::MidiBuffer& buffer) : next(buffer.cbegin()), end(buffer.cend()) {}
    bool dispatch(LiveMidiPerformance* performance, int offset, std::uint64_t absoluteClock, LiveMidiInputs* inputs = nullptr) {
        bool changed = false;
        while (next != end && (*next).samplePosition <= offset) {
            const auto message = *next;
            if (performance != nullptr) {
                const auto accepted = applyLiveMidi(*performance, message.data, message.numBytes, absoluteClock);
                changed = changed || accepted;
            }
            if (inputs != nullptr) { changed = inputs->apply(message.data, message.numBytes, absoluteClock, &applyLiveMidi) || changed; }
            ++next;
        }
        return changed;
    }
private:
    juce::MidiBufferIterator next, end;
};

inline osci::Point sampleLiveMidiAudition(const PreparedComposition& composition, const PreparedClip& clip, const LiveMidiPerformance& performance, double time, std::uint64_t clock, double rate, bool advancing = false, const LiveSourceFrames* liveFrames = nullptr) {
    if (!std::isfinite(time) || !std::isfinite(rate) || rate <= 0) { return {0, 0, 0, 0, 0, 0}; }
    const auto phaseAt = [rate](std::uint64_t index) { return std::fmod(static_cast<double>(index) * 60 / rate, 1.0); };
    const auto current = performance.select(clock, phaseAt(clock));
    if (current.note == 0) { return {0, 0, 0, 0, 0, 0}; }
    const auto previous = clock == 0 ? LiveMidiPerformance::Selection{} : performance.select(clock - 1, phaseAt(clock - 1));
    const auto next = clock == std::numeric_limits<std::uint64_t>::max() ? LiveMidiPerformance::Selection{}
        : performance.select(clock + 1, phaseAt(clock + 1));
    // A clip can be auditioned outside its timeline interval. Its nearest
    // content position supplies geometry/transforms while note age stays live.
    const auto position = std::clamp(time, clip.start, std::nextafter(clip.end, clip.start));
    auto point = composition.projectPoint(clip.sample(position, current.phase, current.phaseSpan, advancing ? 1 / rate : 0, liveFrames), position);
    bool blank = previous.note != current.note || next.note != current.note;
    if (advancing) {
        const auto frame = std::round(time * rate);
        const auto before = std::clamp((frame - 1) / rate, clip.start, std::nextafter(clip.end, clip.start));
        const auto after = std::clamp((frame + 1) / rate, clip.start, std::nextafter(clip.end, clip.start));
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
