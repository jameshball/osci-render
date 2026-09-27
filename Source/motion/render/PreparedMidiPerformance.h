#pragma once

#include "PreparedMidiSchedule.h"
#include "SampleClock.h"
#include "PreparedMidiInstrument.h"

namespace motion {
class PreparedMidiPerformance {
public:
    struct Selection {
        Id note = 0;
        double phase = 0, phaseSpan = 0;
    };
    struct Result {
        std::shared_ptr<const PreparedMidiPerformance> performance;
        std::string error;
        explicit operator bool() const { return performance != nullptr; }
    };
    static Result prepare(const MidiNotes& notes, const Clip& clip, double bpm, double sampleRate, const std::atomic<bool>* cancel = nullptr, const ClipTiming* resolvedTiming = nullptr) {
        const auto envelope = PreparedMidiInstrument::prepareEnvelope(clip.instrument, sampleRate, cancel);
        if (!envelope) { return {nullptr, "Could not prepare the MIDI voice envelope."}; }
        const auto schedule = PreparedMidiSchedule::prepare(notes, clip, bpm, sampleRate, envelope->releaseSamples(), cancel, resolvedTiming);
        if (!schedule) { return {nullptr, schedule.error}; }
        auto result = std::shared_ptr<PreparedMidiPerformance>(new PreparedMidiPerformance(*envelope, schedule.schedule, sampleRate));
        for (std::uint32_t index = 0; index < schedule.schedule->voiceCount(); ++index) {
            const auto pitch = schedule.schedule->voice(index).pitch;
            if (result->pitches[static_cast<std::size_t>(pitch)]) { continue; }
            const auto frequency = 440 * std::exp2((pitch - 69) / 12.0);
            auto voice = osci_audio::PreparedNoteVoice::prepare(*envelope, frequency, 1, 1);
            if (!voice) { return {nullptr, "A MIDI pitch exceeds the output sample rate's Nyquist limit."}; }
            result->pitches[static_cast<std::size_t>(pitch)] = std::move(voice);
        }
        return {std::move(result), {}};
    }

    Selection select(double time, double allocationPhase, double oscillatorTime) const {
        const auto sample = sampleIndex(time, sampleRate), oscillatorSample = sampleIndex(oscillatorTime, sampleRate);
        if (!sample || !oscillatorSample || !std::isfinite(allocationPhase) || allocationPhase < 0 || allocationPhase >= 1) { return {}; }
        const auto active = schedule->activeAt(*sample);
        auto cursor = allocationPhase * active.size();
        for (const auto index : active) {
            const auto& note = schedule->voice(index);
            const auto gain = envelope.at(note.age(*sample), note.heldSamples()).gain * (note.velocity / 127.0);
            if (cursor < gain) {
                const auto& voice = *pitches[static_cast<std::size_t>(note.pitch)];
                const auto value = voice.at(note.age(*oscillatorSample), note.heldSamples());
                return {note.id, value.phase, value.phaseSpan};
            }
            cursor -= gain;
        }
        return {}; // Unused drawing budget remains dark.
    }

private:
    PreparedMidiPerformance(osci_audio::PreparedVoiceEnvelope envelope, std::shared_ptr<const PreparedMidiSchedule> schedule, double sampleRate)
        : envelope(std::move(envelope)), schedule(std::move(schedule)), sampleRate(sampleRate) {}
    osci_audio::PreparedVoiceEnvelope envelope;
    std::shared_ptr<const PreparedMidiSchedule> schedule;
    std::array<std::optional<osci_audio::PreparedNoteVoice>, 128> pitches;
    double sampleRate;
};
}
