#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/model/TapTempo.h"
#include "../Source/motion/model/TempoDetection.h"

class MotionTempoToolsTest : public juce::UnitTest {
public:
    MotionTempoToolsTest() : juce::UnitTest("Motion tempo tools", "Motion") {}

    // A drum loop: kick on each bar's first beat, snare on 2 and 4, hats on
    // every beat, starting `offset` seconds in.
    static std::vector<float> groove(double bpm, double offset, double seconds, int beatsPerBar = 4) {
        const double rate = 48000;
        std::vector<float> audio(static_cast<std::size_t>(seconds * rate), 0.0f);
        juce::Random random(7);
        const auto beat = 60 / bpm;
        for (int index = 0; offset + index * beat < seconds; ++index) {
            const auto start = static_cast<std::size_t>((offset + index * beat) * rate);
            const auto position = index % beatsPerBar;
            for (std::size_t i = 0; i < 6000 && start + i < audio.size(); ++i) {
                const auto t = static_cast<double>(i) / rate;
                auto sample = 0.3 * (random.nextFloat() * 2 - 1) * std::exp(-t * 90); // hat
                if (position == 0) { sample += 0.9 * std::sin(2 * juce::MathConstants<double>::pi * 55 * t) * std::exp(-t * 18); }
                if (position == 1 || position == 3) { sample += 0.5 * (random.nextFloat() * 2 - 1) * std::exp(-t * 30); }
                audio[start + i] += static_cast<float>(sample);
            }
        }
        return audio;
    }

    void runTest() override {
        beginTest("Tempo, beat phase and downbeat come from a drum loop");
        {
            for (const auto& [bpm, offset] : std::vector<std::pair<double, double>> {{128, 0.3}, {90, 1.1}, {150, 0.0}, {174, 0.52}}) {
                const auto audio = groove(bpm, offset, 30);
                const auto estimate = motion::TempoDetection::estimate(audio, 48000);
                expect(estimate.has_value(), "found a tempo at " + juce::String(bpm));
                if (!estimate.has_value()) { continue; }
                // Drum and bass is as often heard in half time: either reading is right.
                const auto heard = bpm > 160 && std::abs(estimate->bpm - bpm / 2) < 0.05 ? bpm / 2 : bpm;
                expectWithinAbsoluteError(estimate->bpm, heard, 0.05, "tempo at " + juce::String(bpm));
                const auto bar = 4 * 60 / heard;
                const auto expectedDownbeat = std::fmod(offset, bar);
                // In half time the loop's kick lands every half bar, so either half is a downbeat.
                const auto period = heard == bpm ? bar : bar / 2;
                const auto error = std::abs(std::remainder(estimate->firstDownbeat - expectedDownbeat, period));
                expect(error < 0.02, "downbeat at " + juce::String(bpm) + " BPM: " + juce::String(estimate->firstDownbeat) + " vs " + juce::String(expectedDownbeat));
                expect(estimate->confidence > 0.4, "a clean loop is confident");
            }
            std::vector<float> silence(48000 * 10, 0.0f);
            const auto none = motion::TempoDetection::estimate(silence, 48000);
            expect(!none.has_value() || none->confidence < 0.4, "silence has no confident tempo");
            expect(!motion::TempoDetection::estimate(std::vector<float>(1000, 0.0f), 48000).has_value(), "too short");
        }
        beginTest("Tap tempo takes the median interval and restarts after a pause");
        {
            motion::TapTempo taps;
            expect(!taps.tap(0.0).has_value());
            expect(!taps.tap(0.5).has_value(), "two taps are not enough");
            const auto three = taps.tap(1.0);
            expect(three.has_value() && *three == 120.0);
            const auto wobble = taps.tap(1.52); // one late tap barely moves the median
            expect(wobble.has_value() && *wobble == 120.0);
            expect(!taps.tap(5.0).has_value(), "a long pause starts again");
            expectEquals(static_cast<int>(taps.count()), 1);
        }
        beginTest("Durations read and write in the ruler's units, on the tempo map");
        {
            motion::TimeGrid grid;
            grid.display = motion::TimeDisplay::beats;
            grid.bpm = 120;
            grid.beatsPerBar = 4;
            grid.tempoChanges = std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{8, 60}});
            expectEquals(juce::String(grid.durationLabel(0, 2)), juce::String("1.0.000"));
            expectEquals(juce::String(grid.durationLabel(2, 6)), juce::String("1.2.000"), "two beats at 120 then two at 60");
            const auto length = grid.parseDuration("1.2", 2);
            expect(length.has_value() && std::abs(*length - 4.0) < 1e-9);
            const auto seconds = grid.parseDuration("1.5s", 2);
            expect(seconds.has_value() && std::abs(*seconds - 1.5) < 1e-9);
            expect(!grid.parseDuration("0.0", 0).has_value(), "zero length");
            expect(!grid.parseDuration("1.x", 0).has_value());
            grid.display = motion::TimeDisplay::seconds;
            expectEquals(juce::String(grid.durationLabel(1, 3.25)), juce::String("2.250s"));
        }
        beginTest("Setting tempo from audio moves the soundtrack onto the bar grid in one undo step");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            motion::Clip sound;
            sound.id = document.newId(); sound.start = 0; sound.duration = 20;
            motion::Track track;
            track.id = document.newId(); track.name = "Soundtrack"; track.kind = motion::TrackKind::audio;
            track.insert(sound, motion::Tempo(100));
            motion::Project project;
            project.bpm = 100; project.duration = 20; project.tracks = {track};
            document.reset(project);
            double moved = 0;
            expect(document.setTempoFromAudio(sound.id, 120, 0.3, moved).wasOk());
            expectEquals(document.project().bpm, 120.0);
            expectWithinAbsoluteError(moved, 1.7, 1e-9, "the downbeat moves to bar 2, never cutting audio");
            expectWithinAbsoluteError(document.project().tracks[0].clips[0].timing(document.project().tempo()).start, 1.7, 1e-9);
            expect(undo.getUndoDescription() == "Set tempo from soundtrack");
            expect(document.project().timeDisplay == motion::TimeDisplay::beats, "the ruler switches to bars in the same step");
            expect(undo.undo());
            expectEquals(document.project().bpm, 100.0);
            expectEquals(document.project().tracks[0].clips[0].start, 0.0);
            expect(document.setTempoFromAudio(sound.id, 150, 0.0, moved).wasOk());
            expectEquals(moved, 0.0, "already on the grid");
        }
        beginTest("A MIDI file's tempo map can replace the project's");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            motion::Project project;
            project.duration = 20;
            document.reset(project);
            const auto changes = std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{16, 90}, {32, 140, true}});
            expect(document.setTempoMap(100, changes, "Use MIDI tempo", true).wasOk());
            expectEquals(document.project().bpm, 100.0);
            expect(document.project().timeDisplay == motion::TimeDisplay::beats);
            expect(document.project().tempoChanges != nullptr && *document.project().tempoChanges == *changes);
            expect(undo.undo());
            expect(document.project().tempoChanges == nullptr);
        }
    }
};

static MotionTempoToolsTest motionTempoToolsTest;
