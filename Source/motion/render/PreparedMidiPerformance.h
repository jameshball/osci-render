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
    static Result prepare(const MidiNotes& notes, const Clip& clip, const Tempo& tempo, double sampleRate, const std::atomic<bool>* cancel = nullptr, const ClipTiming* resolvedTiming = nullptr) {
        const auto envelope = PreparedMidiInstrument::prepareEnvelope(clip.instrument, sampleRate, cancel);
        if (!envelope) { return {nullptr, "Could not prepare the MIDI voice envelope."}; }
        const auto schedule = PreparedMidiSchedule::prepare(notes, clip, tempo, sampleRate, envelope->releaseSamples(), cancel, resolvedTiming);
        if (!schedule) { return {nullptr, schedule.error}; }
        auto result = std::shared_ptr<PreparedMidiPerformance>(new PreparedMidiPerformance(*envelope, schedule.schedule, sampleRate));
        // Controller changes as sample-indexed steps on the clip's own clock.
        const auto timing = resolvedTiming != nullptr ? *resolvedTiming : clip.timing(tempo);
        const auto secondsPerBeat = 60 / clip.curveBpm(tempo);
        const auto sampleAt = [&](double beat) {
            const auto seconds = timing.projectTime(beat * secondsPerBeat);
            return std::max(0.0, std::round(seconds * sampleRate));
        };
        for (const auto& control : notes.controls()) {
            const auto channel = static_cast<std::size_t>(control.channel - 1);
            if (control.number == MidiControl::pitchBend) {
                result->bends[channel].add(sampleAt(control.beat), std::exp2(clip.instrument.bendRange * control.normalised() / 12));
            } else if (control.number == 11) {
                result->expression[channel].push_back({sampleAt(control.beat), control.normalised()});
            }
        }
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

    std::size_t activeCount(double time) const {
        const auto sample = sampleIndex(time, sampleRate);
        return sample ? schedule->activeAt(*sample).size() : 0;
    }

    Selection select(double time, double allocationPhase, double oscillatorTime) const {
        const auto sample = sampleIndex(time, sampleRate), oscillatorSample = sampleIndex(oscillatorTime, sampleRate);
        if (!sample || !oscillatorSample || !std::isfinite(allocationPhase) || allocationPhase < 0 || allocationPhase >= 1) { return {}; }
        const auto active = schedule->activeAt(*sample);
        auto cursor = allocationPhase * active.size();
        for (const auto index : active) {
            const auto& note = schedule->voice(index);
            const auto channel = static_cast<std::size_t>(std::clamp(note.channel, 1, 16) - 1);
            const auto gain = envelope.at(note.age(*sample), note.heldSamples()).gain * (note.velocity / 127.0) * expressionAt(channel, *sample);
            if (cursor < gain) {
                const auto& voice = *pitches[static_cast<std::size_t>(note.pitch)];
                const auto value = voice.at(note.age(*oscillatorSample), note.heldSamples());
                if (bends[channel].empty()) { return {note.id, value.phase, value.phaseSpan}; }
                // Pitch bend: the phase integrates the bent frequency over the
                // note's life, so bends glide instead of jumping phase.
                const auto now = static_cast<double>(*oscillatorSample);
                const auto started = now - static_cast<double>(note.age(*oscillatorSample));
                const auto step = value.frequency / sampleRate;
                const auto cycles = step * (bends[channel].integral(now) - bends[channel].integral(started));
                return {note.id, cycles - std::floor(cycles), step * bends[channel].factorAt(now)};
            }
            cursor -= gain;
        }
        return {}; // Unused drawing budget remains dark.
    }

private:
    // Piecewise-constant frequency factors from pitch-bend changes and their
    // running integral in samples (factor 1 before the first change).
    struct BendTable {
        std::vector<double> samples, factors, integrals;
        bool empty() const { return samples.empty(); }
        void add(double sample, double factor) {
            const auto integral = samples.empty() ? sample : integrals.back() + (sample - samples.back()) * factors.back();
            samples.push_back(sample);
            factors.push_back(factor);
            integrals.push_back(integral);
        }
        std::size_t before(double sample) const { return static_cast<std::size_t>(std::upper_bound(samples.begin(), samples.end(), sample) - samples.begin()); }
        double factorAt(double sample) const { const auto k = before(sample); return k == 0 ? 1.0 : factors[k - 1]; }
        double integral(double sample) const {
            const auto k = before(sample);
            return k == 0 ? sample : integrals[k - 1] + (sample - samples[k - 1]) * factors[k - 1];
        }
    };
    struct Step { double sample, value; };
    double expressionAt(std::size_t channel, std::uint64_t sample) const {
        const auto& steps = expression[channel];
        const auto after = std::upper_bound(steps.begin(), steps.end(), static_cast<double>(sample), [](double value, const Step& step) { return value < step.sample; });
        return after == steps.begin() ? 1.0 : (after - 1)->value;
    }
    std::array<BendTable, 16> bends;
    std::array<std::vector<Step>, 16> expression;
    PreparedMidiPerformance(osci_audio::PreparedVoiceEnvelope envelope, std::shared_ptr<const PreparedMidiSchedule> schedule, double sampleRate)
        : envelope(std::move(envelope)), schedule(std::move(schedule)), sampleRate(sampleRate) {}
    osci_audio::PreparedVoiceEnvelope envelope;
    std::shared_ptr<const PreparedMidiSchedule> schedule;
    std::array<std::optional<osci_audio::PreparedNoteVoice>, 128> pitches;
    double sampleRate;
};
}
