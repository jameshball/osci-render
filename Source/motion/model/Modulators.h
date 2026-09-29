#pragma once

#include "Modulation.h"
#include <algorithm>
#include <set>
#include <string>

namespace motion {
using ModulatorId = std::uint64_t;

// Reusable modulation sources owned by a composition. One modulator can drive
// any number of properties through routes, so several objects share a single
// clock (an LFO, a random walk, the soundtrack, or an envelope that follows
// the notes of a MIDI clip).
enum class ModulatorKind { oscillator, envelope, controller };

struct Modulator {
    ModulatorId id = 0;
    std::string name = "Modulator";
    ModulatorKind kind = ModulatorKind::oscillator;
    // Oscillator: the waveform, clock and seed of `shape`; its amount, mode
    // and enabled flag are unused (each route carries its own depth).
    Modulation shape;
    // Envelope: ADSR in seconds, triggered by the notes of `source` (a clip
    // with a MIDI pattern in the same composition) within a pitch range.
    std::uint64_t source = 0;
    double attack = 0.005, decay = 0.25, sustain = 0.0, release = 0.2;
    // 0 ignores velocity; 1 scales each note's envelope by velocity / 127.
    double velocity = 1.0;
    int lowestPitch = 0, highestPitch = 127;
    // Controller: a CC number (0-127) or MidiControl::pitchBend (128) of the
    // source clip's pattern, on one channel or any (0).
    int controller = 1, controllerChannel = 0;

    bool operator==(const Modulator& other) const {
        return id == other.id && name == other.name && kind == other.kind && shape == other.shape && source == other.source
            && attack == other.attack && decay == other.decay && sustain == other.sustain && release == other.release
            && velocity == other.velocity && lowestPitch == other.lowestPitch && highestPitch == other.highestPitch
            && controller == other.controller && controllerChannel == other.controllerChannel;
    }
    bool valid() const {
        const auto time = [](double value) { return std::isfinite(value) && value >= 0 && value <= 60; };
        return id != 0 && !name.empty() && name.size() <= 120 && static_cast<int>(kind) >= 0 && static_cast<int>(kind) <= 2
            && controller >= 0 && controller <= 128 && controllerChannel >= 0 && controllerChannel <= 16
            && shape.valid() && time(attack) && time(decay) && time(release) && std::isfinite(sustain) && sustain >= 0 && sustain <= 1
            && std::isfinite(velocity) && velocity >= 0 && velocity <= 1
            && lowestPitch >= 0 && highestPitch <= 127 && lowestPitch <= highestPitch;
    }
    // Oscillators and pitch bend are bipolar (-1..1); envelopes, controllers
    // and soundtrack loudness are unipolar (0..1).
    bool unipolar() const {
        if (kind == ModulatorKind::controller) { return controller != 128; }
        return kind == ModulatorKind::envelope || shape.waveform == ModulationWaveform::soundtrack;
    }
};

// A modulator driving one property: value += amount * m (add) or
// value *= 1 + amount * m (multiply), after keys and the property's own
// oscillator.
struct ModulationRoute {
    std::uint64_t id = 0;
    ModulatorId modulator = 0;
    std::uint64_t target = 0;
    std::string property;
    double amount = 1;
    ModulationMode mode = ModulationMode::add;

    bool operator==(const ModulationRoute&) const = default;
    bool valid() const {
        return id != 0 && modulator != 0 && target != 0 && !property.empty() && std::isfinite(amount) && amount >= -1000000 && amount <= 1000000
            && (mode == ModulationMode::add || mode == ModulationMode::multiply);
    }
};

// Prepared, immutable evaluation of one modulator in its composition's time.
// All state is precomputed; value() is allocation-free and pure.
struct PreparedModulator {
    struct Note { double start, end, level; };
    ModulatorKind kind = ModulatorKind::oscillator;
    Modulation shape;
    double bpm = 120;
    std::shared_ptr<const SoundtrackEnvelope> soundtrack; // project time
    std::vector<Note> notes; // composition seconds, sorted by start
    std::vector<std::pair<double, double>> steps; // controller: (seconds, value), sorted
    double attack = 0, decay = 0, sustain = 0, release = 0;

    // Indexes, for each interval between note starts and release ends, the
    // notes that can sound there, so evaluation visits only those (bounded
    // by polyphony, however long another note is held). Build once after
    // filling `notes`.
    void buildIndex() {
        boundaries.clear();
        offsets.clear();
        active.clear();
        std::vector<std::uint32_t> byReach(notes.size());
        for (std::uint32_t index = 0; index < notes.size(); ++index) {
            byReach[index] = index;
            boundaries.push_back(notes[index].start);
            boundaries.push_back(reach(notes[index]));
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        std::sort(byReach.begin(), byReach.end(), [this](auto a, auto b) { return reach(notes[a]) < reach(notes[b]); });
        std::set<std::uint32_t> sounding;
        std::size_t started = 0, ended = 0;
        for (const auto boundary : boundaries) {
            while (started < notes.size() && notes[started].start <= boundary) { sounding.insert(static_cast<std::uint32_t>(started++)); }
            while (ended < byReach.size() && reach(notes[byReach[ended]]) <= boundary) { sounding.erase(byReach[ended++]); }
            offsets.push_back(static_cast<std::uint32_t>(active.size()));
            active.insert(active.end(), sounding.begin(), sounding.end());
        }
        offsets.push_back(static_cast<std::uint32_t>(active.size()));
    }

    // `time` is the owning composition's time; `projectTime` reaches the
    // soundtrack, which always runs on the main timeline.
    double value(double time, double projectTime) const {
        if (!std::isfinite(time)) { return 0; }
        if (kind == ModulatorKind::oscillator) {
            if (shape.waveform == ModulationWaveform::soundtrack) { return soundtrack != nullptr ? soundtrack->at(projectTime) : 0.0; }
            return shape.value(time, bpm);
        }
        if (kind == ModulatorKind::controller) {
            const auto after = std::upper_bound(steps.begin(), steps.end(), time, [](double value, const auto& step) { return value < step.first; });
            return after == steps.begin() ? 0.0 : (after - 1)->second;
        }
        // Loudest active note wins, so chords do not pile up past 1.
        const auto next = std::upper_bound(boundaries.begin(), boundaries.end(), time);
        if (next == boundaries.begin()) { return 0; }
        const auto interval = static_cast<std::size_t>(next - boundaries.begin()) - 1;
        double level = 0;
        for (auto index = offsets[interval]; index < offsets[interval + 1]; ++index) {
            level = std::max(level, envelopeAt(notes[active[index]], time));
        }
        return level;
    }

private:
    double reach(const Note& note) const { return note.end + std::max(0.0, release); }
    std::vector<double> boundaries;
    std::vector<std::uint32_t> offsets, active;
    double held(double age) const {
        if (age < attack) { return attack > 0 ? age / attack : 1.0; }
        age -= attack;
        if (age < decay) { return 1 - (1 - sustain) * (age / decay); }
        return sustain;
    }
    double envelopeAt(const Note& note, double time) const {
        const auto age = time - note.start;
        if (age < 0) { return 0; }
        if (time < note.end) { return held(age) * note.level; }
        const auto released = time - note.end;
        if (release <= 0 || released >= release) { return 0; }
        return held(note.end - note.start) * note.level * (1 - released / release);
    }
};
}
