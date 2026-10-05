#pragma once

#include "PreparedMidiSchedule.h"
#include "SampleClock.h"
#include "PreparedMidiInstrument.h"

namespace motion {
class PreparedMidiPerformance {
public:
    struct Result {
        std::shared_ptr<const PreparedMidiPerformance> performance;
        std::string error;
        explicit operator bool() const { return performance != nullptr; }
    };
    // `instrument` is the clip's, prepared at the output rate.
    static Result prepare(const MidiNotes& notes, const Clip& clip, std::shared_ptr<const PreparedMidiInstrument> instrument, const Tempo& tempo, const std::atomic<bool>* cancel = nullptr, const ClipTiming* resolvedTiming = nullptr) {
        if (instrument == nullptr || !instrument->envelope) { return {nullptr, "Could not prepare the MIDI voice envelope."}; }
        const auto sampleRate = instrument->sampleRate;
        const auto schedule = PreparedMidiSchedule::prepare(notes, clip, tempo, sampleRate, instrument->envelope->releaseSamples(), cancel, resolvedTiming);
        if (!schedule) { return {nullptr, schedule.error}; }
        for (std::uint32_t index = 0; index < schedule.schedule->voiceCount(); ++index) {
            if (!instrument->pitches[static_cast<std::size_t>(schedule.schedule->voice(index).pitch)]) { return {nullptr, "A MIDI pitch exceeds the output sample rate's Nyquist limit."}; }
        }
        auto result = std::shared_ptr<PreparedMidiPerformance>(new PreparedMidiPerformance(std::move(instrument), schedule.schedule));
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
                result->bends[channel].add(sampleAt(control.beat), result->instrument->bendFactor(control.normalised()));
            } else if (control.number == 11) {
                result->expression[channel].push_back({sampleAt(control.beat), control.normalised()});
            }
        }
        return {std::move(result), {}};
    }

    std::size_t activeCount(double time) const {
        const auto sample = sampleIndex(time, instrument->sampleRate);
        return sample ? schedule->activeAt(*sample).size() : 0;
    }

    MidiSelection select(double time, double allocationPhase, double oscillatorTime) const {
        const auto sample = sampleIndex(time, instrument->sampleRate), oscillatorSample = sampleIndex(oscillatorTime, instrument->sampleRate);
        if (!sample || !oscillatorSample || !std::isfinite(allocationPhase) || allocationPhase < 0 || allocationPhase >= 1) { return {}; }
        const auto active = schedule->activeAt(*sample);
        auto cursor = allocationPhase * active.size();
        for (const auto index : active) {
            const auto& note = schedule->voice(index);
            const auto channel = static_cast<std::size_t>(std::clamp(note.channel, 1, 16) - 1);
            const auto gain = instrument->envelope->at(note.age(*sample), note.heldSamples()).gain * (note.velocity / 127.0) * expressionAt(channel, *sample);
            if (cursor < gain) {
                const auto age = note.age(*oscillatorSample);
                const auto& bend = bends[channel];
                if (bend.empty()) { return instrument->voice(note.id, note.pitch, age, note.heldSamples()); }
                const auto now = static_cast<double>(*oscillatorSample);
                return instrument->bentVoice(note.id, note.pitch, bend.integral(now) - bend.integral(now - static_cast<double>(age)), bend.factorAt(now));
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
    PreparedMidiPerformance(std::shared_ptr<const PreparedMidiInstrument> instrument, std::shared_ptr<const PreparedMidiSchedule> schedule)
        : instrument(std::move(instrument)), schedule(std::move(schedule)) {}
    std::shared_ptr<const PreparedMidiInstrument> instrument;
    std::shared_ptr<const PreparedMidiSchedule> schedule;
};
}
