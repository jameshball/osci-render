#include <JuceHeader.h>
#include "../Source/motion/render/LiveMidiPerformance.h"
#include "../Source/motion/render/LiveMidiAudition.h"
#include "../Source/motion/export/SignalExporter.h"
#include "../Source/motion/render/CompositionPreparationWorker.h"
#include "../Source/motion/render/BeamTransitionGuard.h"

class MotionMidiRenderTest : public juce::UnitTest {
public:
    MotionMidiRenderTest() : juce::UnitTest("Motion MIDI signal rendering", "MotionMidi") {}
    void runTest() override {
        testLiveMidi();
        testLiveMidiInputAndSampling();
        testLiveMidiBufferDispatch();
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
    void testLiveMidiBufferDispatch() {
        beginTest("Actual MIDI buffer offsets dispatch once at their absolute device sample");
        motion::LiveMidiPerformance live;
        expect(live.prepare(48000));
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 5);
        block.addEvent(juce::MidiMessage::noteOff(1, 69), 11);
        motion::LiveMidiInputCursor cursor(block);
        for (int offset = 0; offset < 16; ++offset) {
            const auto changed = cursor.dispatch(&live, offset, 1000 + offset);
            expect(changed == (offset == 5 || offset == 11));
            const auto selected = live.select(1000 + offset, .1);
            if (offset < 5 || offset >= 11) { expect(selected.note == 0); }
            if (offset == 9) {
                expect(selected.note != 0);
                expectWithinAbsoluteError(selected.phase, 4 * 440.0 / 48000, 1e-12);
            }
            expect(!cursor.dispatch(&live, offset, 1000 + offset), "Repeated offsets do not replay events");
        }

        beginTest("A fresh block dispatches offset zero without adding its offset twice");
        juce::MidiBuffer nextBlock;
        nextBlock.addEvent(juce::MidiMessage::noteOn(1, 81, static_cast<juce::uint8>(127)), 0);
        motion::LiveMidiInputCursor nextCursor(nextBlock);
        expect(nextCursor.dispatch(&live, 0, 1016));
        const auto onset = live.select(1016, .1);
        expect(onset.note != 0);
        expectEquals(onset.phase, 0.0);
        expect(!nextCursor.dispatch(&live, 4, 1020));
        expectWithinAbsoluteError(live.select(1020, .1).phase, 4 * 880.0 / 48000, 1e-12);

        beginTest("Same-offset MIDI buffer insertion order and SysEx filtering are preserved");
        live.reset();
        juce::MidiBuffer ordered;
        ordered.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 5);
        ordered.addEvent(juce::MidiMessage::noteOff(1, 69), 5);
        const unsigned char payload[] {1, 2, 3, 4, 5};
        ordered.addEvent(juce::MidiMessage::createSysExMessage(payload, sizeof(payload)), 5);
        ordered.addEvent(juce::MidiMessage::noteOn(1, 81, static_cast<juce::uint8>(127)), 5);
        motion::LiveMidiInputCursor orderedCursor(ordered);
        expect(orderedCursor.dispatch(&live, 5, 205));
        const auto selected = live.select(209, .9);
        expect(selected.note != 0, "Only the final octave note remains active");
        expectWithinAbsoluteError(selected.phaseSpan, 880.0 / 48000, 1e-12);
        expect(!orderedCursor.dispatch(&live, 6, 206));

        beginTest("Disabled audition discards due input and late dispatch uses the supplied clock");
        live.reset();
        juce::MidiBuffer disabled;
        disabled.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
        disabled.addEvent(juce::MidiMessage::noteOn(1, 81, static_cast<juce::uint8>(127)), 5);
        motion::LiveMidiInputCursor disabledCursor(disabled);
        expect(!disabledCursor.dispatch(nullptr, 0, 300));
        expect(!disabledCursor.dispatch(&live, 1, 301));
        expect(live.select(301, .1).note == 0);
        // The caller catches up overdue input at offset eight. Neither the
        // stored offset five nor the current offset eight is added to clock.
        expect(disabledCursor.dispatch(&live, 8, 308));
        expect(live.select(307, .1).note == 0);
        expectWithinAbsoluteError(live.select(312, .1).phase, 4 * 880.0 / 48000, 1e-12);
        juce::MidiBuffer following;
        following.addEvent(juce::MidiMessage::noteOff(1, 81), 0);
        motion::LiveMidiInputCursor followingCursor(following);
        expect(followingCursor.dispatch(&live, 0, 316));
        expect(live.select(316, .1).note == 0);
        expect(!followingCursor.dispatch(&live, 1, 317));
    }

    void testLiveMidiInputAndSampling() {
        beginTest("Raw live MIDI parser rejects unsupported and malformed messages");
        motion::LiveMidiPerformance live;
        expect(live.prepare(48000));
        const unsigned char on[] {0x90, 69, 127};
        expect(!motion::applyLiveMidi(live, nullptr, 3, 0));
        for (const int size : {-1, 0, 1, 2, 4}) { expect(!motion::applyLiveMidi(live, on, size, 0)); }
        for (const auto message : {std::array<unsigned char, 3>{0xf0, 1, 2}, {0xe0, 0, 64}, {0xa0, 69, 100},
            {0xb0, 1, 127}, {0x90, 128, 127}, {0x90, 69, 128}, {0x10, 69, 127}}) {
            expect(!motion::applyLiveMidi(live, message.data(), 3, 0));
        }
        expect(live.select(10, .1).note == 0);
        expect(motion::applyLiveMidi(live, on, 3, 7));
        expect(live.select(6, .1).note == 0);
        expect(live.select(11, .1).note != 0);
        const unsigned char pedalDown[] {0xb0, 64, 64}, off[] {0x80, 69, 0}, resetControllers[] {0xb0, 121, 0};
        expect(motion::applyLiveMidi(live, pedalDown, 3, 12));
        expect(motion::applyLiveMidi(live, off, 3, 13));
        expect(live.select(14, .1).note != 0);
        expect(motion::applyLiveMidi(live, resetControllers, 3, 15));
        expect(live.select(15, .1).note == 0);
        const unsigned char channel16[] {0x9f, 72, 127}, zeroVelocity[] {0x9f, 72, 0};
        expect(motion::applyLiveMidi(live, channel16, 3, 16));
        expect(motion::applyLiveMidi(live, zeroVelocity, 3, 17));
        expect(live.select(17, .1).note == 0);
        const unsigned char allNotes[] {0xb0, 123, 0}, allSound[] {0xb0, 120, 0};
        expect(motion::applyLiveMidi(live, on, 3, 18));
        expect(motion::applyLiveMidi(live, allNotes, 3, 19));
        expect(live.select(19, .1).note == 0);
        expect(motion::applyLiveMidi(live, on, 3, 20));
        expect(motion::applyLiveMidi(live, allSound, 3, 21));
        expect(live.select(21, .1).note == 0);

        beginTest("Live audition applies the selected source transforms and RGB without changing timeline MIDI");
        const auto project = makeProject();
        motion::PreparedComposition composition(project);
        live.reset();
        expect(live.noteOn(1, 69, 127, 100));
        const auto note = live.select(112, .14);
        const auto expected = composition.projectPoint(composition.clips.front().sample(.123, note.phase, note.phaseSpan), .123);
        const auto actual = motion::sampleLiveMidiAudition(composition, composition.clips.front(), live, .123, 112, 48000);
        expect(actual.x == expected.x && actual.y == expected.y && actual.z == expected.z
            && actual.r == expected.r && actual.g == expected.g && actual.b == expected.b);
        expectEquals(actual.r, .2f);
        const auto timelineBefore = composition.sample(.123, .2);
        live.allSoundOff(1);
        expectEquals(motion::sampleLiveMidiAudition(composition, composition.clips.front(), live, .123, 112, 48000).r, 0.0f);
        const auto timelineAfter = composition.sample(.123, .2);
        expect(timelineBefore.x == timelineAfter.x && timelineBefore.r == timelineAfter.r);

        beginTest("Advancing live audition guards source-frame and camera cuts while frozen audition remains lit");
        live.reset();
        expect(live.noteOn(1, 69, 127, 0));
        auto cameraProject = makeProject();
        motion::Camera firstCamera, secondCamera;
        firstCamera.id = 20; secondCamera.id = 21;
        secondCamera.properties["position.x"] = motion::Curve(1);
        cameraProject.cameras = {firstCamera, secondCamera};
        cameraProject.cameraCuts.push_back({22, 21, 5.0 / 48000, 1 - 5.0 / 48000});
        motion::PreparedComposition cameraComposition(cameraProject);
        for (int sample : {4, 5}) {
            const auto moving = motion::sampleLiveMidiAudition(cameraComposition, cameraComposition.clips.front(), live,
                sample / 48000.0, 1000, 48000, true);
            const auto frozen = motion::sampleLiveMidiAudition(cameraComposition, cameraComposition.clips.front(), live,
                sample / 48000.0, 1000, 48000, false);
            expect(moving.r == 0 && moving.g == 0 && moving.b == 0);
            expect(frozen.r > 0);
            expect(moving.x == frozen.x && moving.y == frozen.y);
        }
        auto frameProject = makeProject();
        std::vector<motion::PointSample> points(32, {-.5f, 0, 0, 1, 1, 1});
        for (std::size_t i = 16; i < points.size(); ++i) { points[i].x = .5f; }
        const auto frames = motion::PreparedPointFrames::create(2.0 / .153, 2, 16, std::move(points));
        const auto timing = motion::FrameTiming::create({53, 100});
        auto frameAsset = std::make_shared<motion::Asset>(*frameProject.assets.front());
        frameAsset->source = std::make_shared<motion::PreparedSource>(frames.source, timing.timing);
        frameAsset->drawing.reset();
        frameProject.assets.front() = frameAsset;
        motion::PreparedComposition frameComposition(frameProject);
        for (int sample : {2543, 2544}) {
            const auto moving = motion::sampleLiveMidiAudition(frameComposition, frameComposition.clips.front(), live,
                sample / 48000.0, 1000, 48000, true);
            const auto frozen = motion::sampleLiveMidiAudition(frameComposition, frameComposition.clips.front(), live,
                sample / 48000.0, 1000, 48000, false);
            expect(moving.r == 0 && moving.g == 0 && moving.b == 0);
            expect(frozen.r > 0);
            expect(moving.x == frozen.x && moving.y == frozen.y);
        }

        beginTest("Live audition blanks both sides of a polyphonic ownership transition");
        live.reset();
        expect(live.noteOn(1, 69, 127, 0));
        expect(live.noteOn(1, 81, 127, 0));
        for (std::uint64_t sample : {399, 400}) {
            const auto point = motion::sampleLiveMidiAudition(composition, composition.clips.front(), live, .123, sample, 48000);
            expect(point.r == 0 && point.g == 0 && point.b == 0);
        }
        expect(motion::sampleLiveMidiAudition(composition, composition.clips.front(), live, .123, 390, 48000).r > 0);
        expect(motion::sampleLiveMidiAudition(composition, composition.clips.front(), live, .123, 410, 48000).r > 0);
        for (const auto invalid : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
            expectEquals(motion::sampleLiveMidiAudition(composition, composition.clips.front(), live, .123, 410, invalid).r, 0.0f);
        }
    }

    void testLiveMidi() {
        motion::LiveMidiPerformance live;
        beginTest("Live audition uses device sample age and independent pitch kernels");
        expect(live.prepare(48000));
        expect(live.noteOn(1, 69, 127, 100));
        expect(live.select(99, .2).note == 0);
        const auto first = live.select(112, .2);
        expect(first.note != 0);
        expectWithinAbsoluteError(first.phase, .11, 1e-12);
        expectWithinAbsoluteError(first.phaseSpan, 440.0 / 48000, 1e-12);
        expect(live.noteOff(1, 69, 113));
        expect(live.select(113, .2).note == 0);
        live.reset();
        expect(live.noteOn(1, 81, 127, 100));
        const auto octave = live.select(112, .2);
        expect(octave.note != 0 && octave.note != first.note);
        expectWithinAbsoluteError(octave.phase, .22, 1e-12);

        beginTest("Live velocity consumes drawing budget and leaves oscillator geometry unchanged");
        live.reset();
        expect(live.noteOn(1, 69, 32, 0));
        int audible = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto selected = live.select(12, (i + .5) / 1000);
            if (selected.note != 0) { ++audible; expectWithinAbsoluteError(selected.phase, .11, 1e-12); }
        }
        expect(audible >= 251 && audible <= 253);

        beginTest("Same-pitch live releases are FIFO and isolated by MIDI channel");
        live.reset();
        expect(live.noteOn(1, 69, 127, 0));
        const auto oldest = live.select(10, .1).note;
        expect(live.noteOn(1, 69, 127, 10));
        const auto younger = live.select(12, .75).note;
        expect(oldest != younger);
        expect(live.noteOn(2, 69, 127, 10));
        const auto otherChannel = live.select(12, .9).note;
        expect(live.noteOff(1, 69, 11));
        expect(live.select(12, .1).note == younger);
        expect(live.select(12, .75).note == otherChannel);
        expect(live.noteOff(1, 69, 12));
        expect(live.select(13, .1).note == otherChannel);
        expect(!live.noteOff(1, 69, 13));
        expect(live.noteOn(2, 69, 0, 14));
        expect(live.select(14, .1).note == 0);

        beginTest("Sustain defers note releases and all-notes-off respects channel pedals");
        live.reset();
        expect(live.sustain(1, true, 0));
        expect(live.noteOn(1, 60, 127, 0));
        const auto sustained = live.select(4, .1).note;
        expect(live.noteOff(1, 60, 5));
        expect(live.select(6, .1).note == sustained);
        expect(live.noteOn(2, 64, 127, 6));
        expect(live.allNotesOff(2, 7));
        expect(live.select(8, .9).note == sustained);
        expect(live.noteOn(1, 67, 127, 8));
        expect(live.allNotesOff(1, 9));
        expect(live.select(10, .75).note != 0);
        expect(live.sustain(1, false, 11));
        expect(live.select(11, .1).note == 0);
        expect(live.noteOn(1, 69, 127, 12));
        expect(live.noteOn(2, 72, 127, 12));
        expect(live.allSoundOff(1));
        const auto remaining = live.select(15, .1);
        expect(remaining.note != 0);
        expectWithinAbsoluteError(remaining.phaseSpan, 440 * std::exp2(3.0 / 12) / 48000, 1e-12);

        beginTest("Thirty-two live voices steal oldest and reuse released slots first");
        live.reset();
        for (int i = 0; i < 32; ++i) { expect(live.noteOn(1, 32 + i, 127, static_cast<std::uint64_t>(i))); }
        const auto stolen = live.select(40, .01).note;
        const auto survivor = live.select(40, 1.5 / 32).note;
        expect(live.noteOn(1, 90, 127, 40));
        expect(live.select(44, .01).note != stolen);
        expect(live.select(44, 1.5 / 32).note == survivor);
        expect(!live.noteOff(1, 32, 45));
        expect(live.noteOff(1, 48, 46));
        const auto replacement = live.select(47, .01).note;
        expect(live.noteOn(2, 91, 127, 48));
        expect(live.select(52, .01).note == replacement, "Released slot wins over stealing another held voice");
        std::array<std::uint64_t, 32> identities;
        for (int i = 0; i < 32; ++i) { identities[static_cast<std::size_t>(i)] = live.select(52, (i + .5) / 32).note; }
        std::sort(identities.begin(), identities.end());
        expect(identities.front() != 0 && std::adjacent_find(identities.begin(), identities.end()) == identities.end());

        beginTest("Invalid live events, stale timestamps and reset are bounded");
        live.reset();
        for (const int channel : {-1, 0, 17}) {
            expect(!live.noteOn(channel, 69, 127, 0));
            expect(!live.noteOff(channel, 69, 0));
            expect(!live.sustain(channel, true, 0));
            expect(!live.allNotesOff(channel, 0));
            expect(!live.allSoundOff(channel));
        }
        expect(!live.noteOn(1, -1, 127, 0));
        expect(!live.noteOn(1, 128, 127, 0));
        expect(!live.noteOn(1, 69, -1, 0));
        expect(!live.noteOn(1, 69, 128, 0));
        expect(live.noteOn(1, 69, 127, 100));
        expect(!live.noteOff(1, 69, 99));
        expect(live.select(104, .1).note != 0);
        for (const auto phase : {-1.0, 1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            expect(live.select(104, phase).note == 0);
        }
        live.reset();
        expect(live.select(104, .1).note == 0);
        expect(!live.prepare(std::numeric_limits<double>::quiet_NaN()));
        expect(!live.noteOn(1, 69, 127, 0));
        expect(live.prepare(8000));
        expect(!live.noteOn(1, 127, 127, 0));
        expect(live.noteOn(1, 69, 127, 0));
        const auto finalTick = live.select(std::numeric_limits<std::uint64_t>::max(), .1);
        expect(finalTick.note != 0 && std::isfinite(finalTick.phase) && finalTick.phase >= 0 && finalTick.phase < 1);
    }

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
