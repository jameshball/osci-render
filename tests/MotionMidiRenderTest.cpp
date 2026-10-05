#include <JuceHeader.h>
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/render/LiveMidiPerformance.h"
#include "../Source/motion/render/LiveMidiAudition.h"
#include "../Source/motion/export/SignalExporter.h"
#include "../Source/motion/export/SoundtrackExporter.h"
#include "../Source/motion/render/CompositionPreparationWorker.h"
#include "../Source/motion/render/BeamTransitionGuard.h"
#include "../Source/motion/render/PreparedSoundtrack.h"

class MotionMidiRenderTest : public juce::UnitTest {
public:
    MotionMidiRenderTest() : juce::UnitTest("Motion MIDI signal rendering", "MotionMidi") {}
    void runTest() override {
        testAuthoredEnvelope();
        testNestedAuthoring();
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
        {
            // Voice changes inside the beam's MIDI segment are dark on both sides.
            motion::BeamRenderer beam;
            int jumps = 0;
            osci::Point previous;
            for (std::int64_t index = 5000; index < 5800; ++index) {
                const auto point = beam.sample(voices, index / 48000.0, index, 48000, true, 1);
                if (index > 5000 && point.r > 0 && previous.r > 0 && std::abs(point.x - previous.x) > .2f) { ++jumps; }
                previous = point;
            }
            expectEquals(jumps, 0, "No lit sample connects two voices");
        }

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
                motion::BeamRenderer beam;
                for (const int index : {0, 1, 123, 4095, 8000, 11025, 22050, 30000}) {
                    const auto value = beam.sample(exact, index / 44100.0, index, 44100, true, 1);
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
    void testAuthoredEnvelope() {
        beginTest("Authored ADSR matches live and seekable MIDI allocation through attack, sustain and release");
        for (const double rate : {44100.0, 48000.0}) {
            auto project = makeProject();
            auto& clip = project.tracks[0].clips[0];
            clip.start = 0; clip.duration = 2; clip.offset = 0; clip.rate = 1;
            clip.instrument = {.05, .1, .35, .2};
            clip.midi = motion::MidiNotes::create({{1, 0, .8, 69, 127, 1}}).source;
            const auto prepared = motion::PreparedMidiPerformance::prepare(*clip.midi, clip, motion::Tempo(120), rate);
            expect(static_cast<bool>(prepared), juce::String(prepared.error));
            motion::LiveMidiPerformance live;
            expect(live.prepare(rate, clip.instrument));
            expect(live.noteOn(1, 69, 127, 0));
            expect(live.noteOff(1, 69, static_cast<std::uint64_t>(.4 * rate)));
            if (!prepared) { continue; }
            for (const double seconds : {.001, .025, .075, .2, .399, .4, .45, .55, .61}) {
                const auto sample = static_cast<std::uint64_t>(std::round(seconds * rate));
                const auto time = sample / rate;
                for (const double phase : {.01, .2, .34, .36, .5, .9}) {
                    const auto direct = live.select(sample, phase);
                    const auto timeline = prepared.performance->select(time, phase, time);
                    expect((direct.note != 0) == (timeline.note != 0), "Live/timeline envelope disagreement at " + juce::String(time));
                    if (direct.note != 0 && timeline.note != 0) { expectWithinAbsoluteError(direct.phase, timeline.phase, 1e-10); }
                }
            }
            expect(live.select(static_cast<std::uint64_t>(.45 * rate), .01).note != 0, "Release tail remains audible");
            expect(live.select(static_cast<std::uint64_t>(.61 * rate), .01).note == 0);
            const auto changed = motion::PreparedMidiInstrument::prepare({.1, .2, .5, .3}, rate);
            expect(changed.has_value());
            if (changed) { live.useInstrument(*changed); expect(live.select(static_cast<std::uint64_t>(.45 * rate), .01).note == 0); expect(live.usesInstrument(*changed)); }
            motion::PreparedComposition composition(project, rate);
            expect(composition.preparationError.isEmpty(), composition.preparationError);
            expect(composition.clips[0].liveInstrument != nullptr);
            if (composition.clips[0].liveInstrument != nullptr) { expect(composition.clips[0].liveInstrument->settings == clip.instrument); }
            juce::TemporaryFile exported(".wav");
            std::atomic<bool> cancel {false};
            const auto result = motion::SignalExporter::write(project, exported.getFile(), rate, cancel);
            expect(result.wasOk(), result.getErrorMessage());
            if (result.wasOk()) {
                juce::WavAudioFormat format;
                auto stream = exported.getFile().createInputStream();
                std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
                expect(reader != nullptr);
                if (reader) {
                    juce::AudioBuffer<float> signal(5, static_cast<int>(reader->lengthInSamples));
                    expect(reader->read(signal.getArrayOfWritePointers(), 5, 0, signal.getNumSamples()));
                    motion::BeamRenderer beam;
                    for (const double seconds : {.001, .025, .075, .2, .399, .4, .45, .55, .61}) {
                        const int index = static_cast<int>(std::round(seconds * rate));
                        const auto time = index / rate;
                        const auto point = beam.sample(composition, time, index, rate, true, 1);
                        expectEquals(signal.getSample(0, index), point.x);
                        expectEquals(signal.getSample(2, index), point.r);
                    }
                }
            }
        }
    }

    void testNestedAuthoring() {
        beginTest("Precomposing animated MIDI and soundtrack together preserves both signal clocks");
        auto project = makeProject();
        project.duration = 8; project.bpm = 96;
        auto& visual = project.tracks.front().clips.front();
        visual.start = 1; visual.duration = 2; visual.offset = .125; visual.rate = 1.25;
        visual.midi = motion::MidiNotes::create({{1, 0, 1, 69, 127, 1}, {2, 1, 2, 72, 96, 1}, {3, 2, 2, 76, 127, 2}}).source;
        visual.properties["position.y"].setKey({0, -.2, motion::Interpolation::linear});
        visual.properties["position.y"].setKey({4, .4, motion::Interpolation::linear});
        std::vector<motion::PointSample> points(32, {-.5f, 0, 0, 1, 1, 1});
        for (std::size_t i = 0; i < points.size(); ++i) {
            points[i].x = (i < 16 ? -.5f : .5f) + static_cast<float>(i % 16) / 80.0f;
            points[i].y = i < 16 ? 0 : .25f;
        }
        const auto frames = motion::PreparedPointFrames::create(4, 2, 16, std::move(points));
        const auto frameTiming = motion::FrameTiming::create({125, 375});
        auto animated = std::make_shared<motion::Asset>(*project.assets.front());
        animated->source = std::make_shared<motion::PreparedSource>(frames.source, frameTiming.timing);
        animated->drawing.reset(); project.assets.front() = animated;
        std::vector<float> pcm(32000);
        for (std::size_t i = 0; i < pcm.size(); ++i) { pcm[i] = static_cast<float>(std::sin(i * .017) * .3); }
        const std::array<std::span<const float>, 1> channels {pcm};
        const auto audio = motion::PreparedAudio::fromPlanar(8000, channels);
        auto soundtrack = std::make_shared<motion::Asset>(); soundtrack->id = 10; soundtrack->name = "Reference"; soundtrack->audio = audio.audio;
        project.assets.push_back(soundtrack);
        motion::Clip sound; sound.id = 11; sound.asset = 10; sound.start = .75; sound.duration = 3; sound.offset = .2; sound.rate = .8;
        sound.properties["gain"].setKey({0, .2, motion::Interpolation::linear});
        sound.properties["gain"].setKey({4, .8, motion::Interpolation::linear});
        sound.properties["pan"] = motion::Curve(.3);
        motion::Track audioTrack; audioTrack.id = 12; audioTrack.kind = motion::TrackKind::audio; audioTrack.clips = {sound};
        project.tracks.push_back(audioTrack);
        const motion::PreparedComposition before(project, 48000);
        const motion::PreparedSoundtrack beforeAudio(project);
        expect(before.preparationError.isEmpty(), before.preparationError);
        juce::UndoManager undo; motion::Document document(undo); document.reset(project);
        motion::Id instance = 0;
        auto result = document.createComposition({2, 11}, "Audiovisual motif", instance);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) { return; }
        motion::Id outer = 0;
        result = document.createComposition({instance}, "Nested motif", outer);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) { return; }
        const motion::PreparedComposition nested(document.mainProject(), 48000);
        const motion::PreparedSoundtrack nestedAudio(document.mainProject());
        expect(nested.preparationError.isEmpty(), nested.preparationError);
        expect(nestedAudio.preparationError.empty());
        int lit = 0, audible = 0;
        // Non-monotonic probes include trims, note/frame changes and exclusive ends.
        for (const auto time : {2.8, .749, .75, 1.0, 1.0001, 1.399, 1.4, 2.0, 3.0, 3.749, 3.75, 1.123}) {
            for (const auto phase : {.01, .19, .51, .93}) {
                const auto a = before.sample(time, phase), b = nested.sample(time, phase);
                expectWithinAbsoluteError(b.x, a.x, .00001f); expectWithinAbsoluteError(b.y, a.y, .00001f);
                expectWithinAbsoluteError(b.r, a.r, .00001f); expectWithinAbsoluteError(b.g, a.g, .00001f); expectWithinAbsoluteError(b.b, a.b, .00001f);
                lit += b.r > 0;
            }
            const auto a = beforeAudio.sample(time), b = nestedAudio.sample(time);
            expectWithinAbsoluteError(b.left, a.left, .00001f); expectWithinAbsoluteError(b.right, a.right, .00001f);
            audible += std::abs(b.left) > .001f;
        }
        expect(lit > 0 && audible > 0, "The equivalence probes must exercise nonblank visual and audio output");
        beginTest("Nested animated MIDI and soundtrack exports match ungrouped output at every sample");
        // A second instance begins a quarter-second into the definition and
        // runs at 1.5x. Build the expected flat timing independently, rather
        // than invoking the same nesting helper used by preparation.
        auto repeated = document.mainProject();
        auto copy = repeated.tracks.front().clips.front();
        copy.id = document.newId(); copy.start = 4; copy.duration = 1.5; copy.offset = 1; copy.rate = 1.5;
        repeated.tracks.front().clips.push_back(copy);
        auto flat = project;
        auto flatVisual = flat.tracks[0].clips.front();
        flatVisual.id = copy.id + 1; flatVisual.start = 4; flatVisual.duration = 2.0 / 1.5; flatVisual.rate = 1.875;
        flat.tracks[0].clips.push_back(flatVisual);
        auto flatAudio = flat.tracks[1].clips.front();
        flatAudio.id = copy.id + 2; flatAudio.start = 4; flatAudio.duration = 1.5; flatAudio.offset = .4; flatAudio.rate = 1.2;
        flat.tracks[1].clips.push_back(flatAudio);
        constexpr double exportRate = 44100;
        const motion::PreparedComposition directExport(flat, exportRate);
        const motion::PreparedSoundtrack flatSoundtrack(flat);
        expect(directExport.preparationError.isEmpty(), directExport.preparationError);
        expect(flatSoundtrack.preparationError.empty());
        std::atomic<bool> cancel {false};
        for (const bool signal : {true, false}) {
            juce::TemporaryFile file(".wav");
            const auto exported = signal
                ? motion::SignalExporter::write(repeated, file.getFile(), exportRate, cancel)
                : motion::SoundtrackExporter::write(repeated, file.getFile(), exportRate, cancel);
            expect(exported.wasOk(), exported.getErrorMessage());
            if (exported.failed()) { continue; }
            juce::WavAudioFormat format;
            auto stream = file.getFile().createInputStream();
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
            expect(reader != nullptr);
            if (reader == nullptr) { continue; }
            const int channelCount = signal ? 5 : 2;
            expectEquals(static_cast<int>(reader->numChannels), channelCount);
            expectEquals(reader->lengthInSamples, static_cast<juce::int64>(project.duration * exportRate));
            juce::AudioBuffer<float> samples(channelCount, static_cast<int>(reader->lengthInSamples));
            expect(reader->read(samples.getArrayOfWritePointers(), channelCount, 0, samples.getNumSamples()));
            float maximumError = 0;
            motion::BeamRenderer directBeam;
            for (int index = 0; index < samples.getNumSamples(); ++index) {
                const auto time = index / exportRate;
                if (signal) {
                    const auto point = directBeam.sample(directExport, time, index, exportRate, true, 1);
                    const std::array<float, 5> expected {point.x, point.y, point.r, point.g, point.b};
                    for (int channel = 0; channel < channelCount; ++channel) {
                        maximumError = std::max(maximumError, std::abs(samples.getSample(channel, index) - expected[channel]));
                    }
                } else {
                    const auto value = flatSoundtrack.sample(time);
                    maximumError = std::max(maximumError, std::abs(samples.getSample(0, index) - value.left));
                    maximumError = std::max(maximumError, std::abs(samples.getSample(1, index) - value.right));
                }
            }
            expect(maximumError < .00001f, juce::String(signal ? "XYRGB" : "Soundtrack") + " maximum sample error " + juce::String(maximumError, 9));
        }
        expect(undo.undo()); expect(undo.undo()); expect(document.mainProject().definitions.empty());
        expect(undo.redo()); expect(undo.redo());
        expect(document.mainProject().definitions.size() == 2);
    }

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
        for (const auto message : {std::array<unsigned char, 3>{0xf0, 1, 2}, {0xe0, 0, 128}, {0xa0, 69, 100},
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
        asset->drawing = std::make_shared<motion::PreparedDrawing>(std::move(shapes));
        motion::Clip clip; clip.id = 2; clip.asset = 1; clip.name = "MIDI line"; clip.duration = 1;
        clip.midi = motion::MidiNotes::create({{1, 0, 1, 69, 127, 1}}).source;
        clip.properties["red"] = motion::Curve(.2); clip.properties["green"] = motion::Curve(.4); clip.properties["blue"] = motion::Curve(.8);
        motion::Track track; track.id = 3; track.insert(std::move(clip), motion::Tempo(120));
        motion::Project project; project.duration = 1;
        project.assets.push_back(std::move(asset)); project.tracks.push_back(std::move(track));
        return project;
    }
};
static MotionMidiRenderTest motionMidiRenderTest;
