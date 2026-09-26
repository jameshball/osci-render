#include <JuceHeader.h>
#include "../Source/motion/export/SignalExporter.h"
#include "../Source/motion/render/CompositionPreparationWorker.h"
#include "../Source/motion/render/BeamTransitionGuard.h"

class MotionMidiRenderTest : public juce::UnitTest {
public:
    MotionMidiRenderTest() : juce::UnitTest("Motion MIDI signal rendering", "MotionMidi") {}
    void runTest() override {
        auto project = makeProject();
        motion::PreparedComposition composition(project, 48000);
        beginTest("MIDI selects source phase from pitch, independent of drawing allocation");
        expect(composition.preparationError.isEmpty(), composition.preparationError);
        expect(composition.hasMidi);
        for (const auto phase : {.05, .25, .5, .9}) {
            const auto point = composition.sample(.123, phase);
            expectWithinAbsoluteError(point.x, 2.12f, .00001f);
            expectWithinAbsoluteError(point.r, .2f, .00001f);
        }
        expectEquals(composition.sample(.5, .2).r, 0.0f, "Released notes leave the clip silent");
        auto heavier = project; heavier.tracks[0].clips[0].properties["weight"] = motion::Curve(10);
        motion::PreparedComposition weighted(heavier, 48000);
        expectWithinAbsoluteError(weighted.sample(.123, .2).x, composition.sample(.123, .2).x, 1e-6f);

        beginTest("Velocity changes drawing budget rather than object size or RGB amplitude");
        auto quiet = project;
        quiet.tracks[0].clips[0].midi = motion::MidiNotes::create({{1, 0, 1, 69, 32, 1}}).source;
        motion::PreparedComposition reduced(quiet, 48000);
        int lit = 0;
        for (int index = 0; index < 1000; ++index) {
            const auto point = reduced.sample(.123, (index + .5) / 1000);
            if (point.r > 0) { ++lit; expectWithinAbsoluteError(point.x, 2.12f, .00001f); expectEquals(point.r, .2f); }
        }
        expect(lit >= 251 && lit <= 253);

        beginTest("Note ownership transitions blank RGB without changing geometry");
        auto chord = project;
        chord.tracks[0].clips[0].midi = motion::MidiNotes::create({{1, 0, 1, 69, 127, 1}, {2, 0, 1, 81, 127, 1}}).source;
        motion::PreparedComposition voices(chord, 48000);
        const auto a = voices.selectBeam(.123, .49), b = voices.selectBeam(.123, .51);
        expect(a.note == 1 && b.note == 2);
        expectWithinAbsoluteError(a.phase, .12, 1e-9); expectWithinAbsoluteError(b.phase, .24, 1e-9);
        const auto unguarded = voices.sample(.123, .5);
        const auto guarded = voices.sample(.123, .5, 60.0 / 48000, 1.0 / 48000);
        expect(unguarded.r > 0); expectEquals(guarded.r, 0.0f); expectEquals(guarded.x, unguarded.x);

        beginTest("Paused audition freezes membership but continues tracing the source");
        const auto frozen = composition.sample(.123, .2, 0, 0, .123);
        const auto audition = composition.sample(.123, .2, 0, 0, .62325);
        expect(frozen.r > 0 && audition.r > 0, "Authored release does not end a frozen audition");
        expect(std::abs(frozen.x - audition.x) > .01f);
        expectEquals(composition.sample(.6, .2, 0, 0, .62325).r, 0.0f, "Membership follows timeline, not audition time");
        motion::PreparedComposition editor(project, 48000, nullptr, motion::CompositionPurpose::editorGeometry);
        expect(!editor.hasMidi);
        expect(editor.clips[0].sample(.123, .2).x != editor.clips[0].sample(.123, .8).x, "Editor still traces complete geometry at frozen time");

        beginTest("Destination-rate export matches direct sampling and rejects mismatched snapshots");
        juce::TemporaryFile exported(".wav");
        std::atomic<bool> cancel {false};
        expect(motion::SignalExporter::write(composition, exported.getFile(), 44100, cancel).failed());
        const auto result = motion::SignalExporter::write(project, exported.getFile(), 44100, cancel);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.wasOk()) {
            juce::WavAudioFormat format;
            auto stream = exported.getFile().createInputStream();
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
            expect(reader != nullptr);
            if (reader) {
                juce::AudioBuffer<float> signal(5, static_cast<int>(reader->lengthInSamples));
                expect(reader->read(signal.getArrayOfWritePointers(), 5, 0, signal.getNumSamples()));
                motion::PreparedComposition exact(project, 44100);
                for (const int index : {0, 1, 123, 4095, 8000, 11025, 22050, 30000}) {
                    const auto time = index / 44100.0, phase = std::fmod(index * 60.0 / 44100, 1.0);
                    const auto value = exact.sample(time, phase, 60.0 / 44100, 1.0 / 44100);
                    expectEquals(signal.getSample(0, index), value.x);
                    expectEquals(signal.getSample(2, index), value.r);
                }
            }
        }

        beginTest("Invalid polyphony fails explicitly and never falls back to continuous source playback");
        std::vector<motion::MidiNote> dense;
        for (motion::Id id = 1; id <= 33; ++id) { dense.push_back({id, 0, 1, 69, 127, 1}); }
        auto invalid = project; invalid.tracks[0].clips[0].midi = motion::MidiNotes::create(dense).source;
        motion::PreparedComposition rejected(invalid, 48000);
        expect(rejected.preparationError.contains("32")); expect(rejected.preparationError.contains("MIDI line"));
        expectEquals(rejected.sample(.1, .2).r, 0.0f);
        expect(motion::SignalExporter::write(rejected, exported.getFile(), 48000, cancel).failed());

        beginTest("Unpredictable transport changes blank at the old position before travelling");
        motion::BeamTransitionGuard guard;
        const osci::Point oldPoint {-1, -2, 0, 1, 1, 1}, newPoint {5, 6, 0, .2f, .4f, .8f};
        guard.apply(oldPoint); guard.apply(oldPoint);
        expectEquals(guard.apply(oldPoint).r, 1.0f);
        guard.begin();
        const auto darkOld = guard.apply(newPoint), darkNew = guard.apply(newPoint), litNew = guard.apply(newPoint);
        expectEquals(darkOld.x, oldPoint.x); expectEquals(darkOld.y, oldPoint.y); expectEquals(darkOld.r, 0.0f);
        expectEquals(darkNew.x, newPoint.x); expectEquals(darkNew.y, newPoint.y); expectEquals(darkNew.r, 0.0f);
        expectEquals(litNew.x, newPoint.x); expectEquals(litNew.r, newPoint.r);

        beginTest("Worker coalesces superseded revisions and returns the requested sample rate");
        juce::WaitableEvent ready;
        motion::CompositionPreparationWorker worker([&] { ready.signal(); });
        std::uint64_t revision = 0;
        for (int i = 0; i < 30; ++i) { revision = worker.request(i == 29 ? project : invalid, i == 29 ? 44100 : 48000); }
        std::unique_ptr<motion::CompositionPreparationWorker::Result> latest;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
        while (juce::Time::getMillisecondCounterHiRes() < deadline) {
            ready.wait(1000);
            auto next = worker.take();
            if (next && next->revision == revision) { latest = std::move(next); break; }
        }
        expect(latest != nullptr);
        if (latest) {
            expect(latest->error.isEmpty(), latest->error);
            expect(latest->composition != nullptr);
            if (latest->composition) { expectEquals(latest->composition->sampleRate, 44100.0); }
        }
    }
private:
    static motion::Project makeProject() {
        auto asset = std::make_shared<motion::Asset>(); asset->id = 1; asset->name = "Line";
        std::vector<std::unique_ptr<osci::Shape>> shapes;
        shapes.push_back(std::make_unique<osci::Line>(osci::Point(2, 0, 0, 1, 1, 1), osci::Point(3, 1, 0, 1, 1, 1)));
        asset->drawing = std::make_shared<osci::PreparedDrawing>(std::move(shapes));
        motion::Clip clip; clip.id = 2; clip.asset = 1; clip.name = "MIDI line"; clip.duration = 1;
        clip.midi = motion::MidiNotes::create({{1, 0, 1, 69, 127, 1}}).source;
        clip.properties["red"] = motion::Curve(.2); clip.properties["green"] = motion::Curve(.4); clip.properties["blue"] = motion::Curve(.8);
        motion::Track track; track.id = 3; track.insert(std::move(clip));
        motion::Project project; project.duration = 1;
        project.assets.push_back(std::move(asset)); project.tracks.push_back(std::move(track));
        return project;
    }
};
static MotionMidiRenderTest motionMidiRenderTest;
