#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/model/MidiTakeNotes.h"
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/render/LiveMidiAudition.h"
#include "../Source/motion/import/MidiSourcePreparer.h"

class MotionMidiExpressionTest : public juce::UnitTest {
public:
    MotionMidiExpressionTest() : juce::UnitTest("Motion MIDI expression and track input", "MotionMidi") {}

    static std::shared_ptr<const motion::MidiNotes> pattern(std::vector<motion::MidiControl> controls, int channel = 1) {
        return motion::MidiNotes::create({{1, 0, 8, 69, 127, channel}}, std::move(controls)).source;
    }
    static std::shared_ptr<motion::Asset> triangle(juce::UndoManager&, motion::Document& document) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = document.newId(); asset->name = "triangle.obj"; asset->extension = ".obj";
        const juce::String obj("v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n");
        asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
        motion::decodeAsset(*asset);
        return asset;
    }

    void runTest() override {
        beginTest("Controller changes hold per channel, with an any-channel view");
        {
            const auto notes = pattern({{2, 1, 1, 64}, {1, 3, 1, 127}, {0.5, 1, motion::MidiControl::pitchBend, 8191}});
            expect(notes != nullptr);
            expectWithinAbsoluteError(notes->controlAt(1, 1, 2.5, -1), 64 / 127.0, 1.0e-12);
            expectWithinAbsoluteError(notes->controlAt(1, 1, 0.5, -1), -1.0, 1.0e-12);
            expectWithinAbsoluteError(notes->controlAt(1, 0, 1.5, -1), 1.0, 1.0e-12); // channel 3 at beat 1
            expectWithinAbsoluteError(notes->controlAt(motion::MidiControl::pitchBend, 1, 4, 0), 8191 / 8192.0, 1.0e-12);
            expect(!motion::MidiNotes::create({}, {{0, 1, 1, 200}}), "values are range-checked");
            const auto moved = notes->moveNotes(std::vector<std::uint64_t> {1}, 1, 0);
            expect(moved && moved.source->controls() == notes->controls(), "note edits keep controller data");
        }
        beginTest("Takes keep pitch bend and controllers, and a channel filter keeps one channel");
        {
            motion::MidiRecording::Take take;
            take.config.token = 1; take.config.target = 7; take.config.firstSample = 0; take.config.endSample = 48000;
            take.config.sampleRate = 48000; take.config.sourceBpm = 120;
            take.firstSample = 0; take.endSample = 48000;
            take.events = {{0, {0x90, 60, 100}, 3}, {2400, {0xe0, 0, 0x60}, 3}, {4800, {0xb1, 11, 90}, 3}, {9600, {0x91, 64, 100}, 3}, {24000, {0x80, 60, 0}, 3}, {24000, {0x81, 64, 0}, 3}};
            const auto all = motion::MidiTakeNotes::convert(take);
            expect(all && all.source->notes().size() == 2 && all.source->controls().size() == 2, juce::String(all.error));
            expectEquals(all.source->controls()[0].number, motion::MidiControl::pitchBend);
            expectEquals(all.source->controls()[0].value, (0x60 << 7) - 8192);
            take.config.channel = 1;
            const auto filtered = motion::MidiTakeNotes::convert(take);
            expect(filtered && filtered.source->notes().size() == 1 && filtered.source->controls().size() == 1);
        }
        beginTest("MIDI files import pitch bend and controllers");
        {
            std::vector<std::uint8_t> track;
            const auto event = [&](std::uint8_t delta, std::initializer_list<std::uint8_t> data) { track.push_back(delta); track.insert(track.end(), data); };
            event(0, {0x90, 60, 100});
            event(0, {0xe0, 0, 0x50});
            event(0x60, {0xb0, 1, 70});
            event(0x60, {0x80, 60, 0});
            event(0, {0xff, 0x2f, 0});
            std::vector<std::uint8_t> file {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, static_cast<std::uint8_t>(track.size())};
            file.insert(file.end(), track.begin(), track.end());
            const auto result = motion::MidiSourcePreparer::prepare(file.data(), file.size(), 120);
            expect(static_cast<bool>(result), juce::String(result.error));
            if (result) {
                expectEquals(static_cast<int>(result.source->controls().size()), 2);
                expectWithinAbsoluteError(result.source->controls()[1].beat, 1.0, 1.0e-12);
                expectEquals(result.source->controls()[1].value, 70);
            }
        }
        beginTest("Pitch bend glides the prepared voice; expression scales its drawing time");
        {
            motion::Clip clip;
            clip.id = 1; clip.duration = 4; clip.instrument.bendRange = 12;
            clip.midi = pattern({{1, 1, motion::MidiControl::pitchBend, 8191}});
            const auto bent = motion::PreparedMidiPerformance::prepare(*clip.midi, clip, motion::Tempo(120), 48000);
            clip.midi = pattern({});
            const auto plain = motion::PreparedMidiPerformance::prepare(*clip.midi, clip, motion::Tempo(120), 48000);
            expect(bent && plain);
            // Before the bend (beat 1 = 0.5 s) they match; after it the span doubles.
            const auto early = bent.performance->select(0.25, 0.1, 0.25), earlyPlain = plain.performance->select(0.25, 0.1, 0.25);
            expectWithinAbsoluteError(early.phase, earlyPlain.phase, 1.0e-9);
            const auto late = bent.performance->select(1.0, 0.1, 1.0), latePlain = plain.performance->select(1.0, 0.1, 1.0);
            expectWithinAbsoluteError(late.phaseSpan / latePlain.phaseSpan, std::exp2(8191 / 8192.0), 1.0e-9);
            // 0.5 s at 440 Hz, then 0.5 s near 880 Hz: about 660 cycles in all.
            const auto cycles = 440 * 0.5 + 440 * std::exp2(8191 / 8192.0) * 0.5;
            expectWithinAbsoluteError(late.phase, cycles - std::floor(cycles), 1.0e-6);
            clip.midi = pattern({{0, 1, 11, 0}});
            const auto silent = motion::PreparedMidiPerformance::prepare(*clip.midi, clip, motion::Tempo(120), 48000);
            expect(silent && silent.performance->select(1.0, 0.1, 1.0).note == 0, "expression 0 leaves the voice undrawn");
        }
        beginTest("Live pitch bend and expression follow incoming messages");
        {
            motion::MidiInstrument settings;
            settings.bendRange = 12;
            motion::LiveMidiPerformance live;
            expect(live.prepare(48000, settings));
            const unsigned char on[] {0x90, 69, 127}, bend[] {0xe0, 0x7f, 0x7f}, quiet[] {0xb0, 11, 0};
            expect(motion::applyLiveMidi(live, on, 3, 100));
            const auto before = live.select(1000, 0.1);
            expect(motion::applyLiveMidi(live, bend, 3, 2000));
            const auto after = live.select(3000, 0.1);
            expectWithinAbsoluteError(after.phaseSpan / before.phaseSpan, std::exp2(8191 / 8192.0), 1.0e-9);
            expect(motion::applyLiveMidi(live, quiet, 3, 3000));
            expect(live.select(4000, 0.1).note == 0);
        }
        beginTest("Controller modulators follow a clip's CC; armed tracks save and draw live notes");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            const auto asset = triangle(undo, document);
            auto clip = motion::Document::makeClip(document.newId(), *asset, 0);
            clip.duration = 4;
            clip.midi = pattern({{2, 1, 1, 127}});
            motion::Track track;
            track.id = document.newId(); track.name = "Keys"; track.midiInput = motion::Track::anyMidiChannel;
            track.insert(clip, motion::Tempo(120));
            motion::Project project;
            project.duration = 10; project.assets = {asset}; project.tracks = {track};
            document.reset(project);
            motion::Modulator wheel;
            wheel.kind = motion::ModulatorKind::controller;
            wheel.source = clip.id;
            wheel.controller = 1;
            motion::Id id = 0, route = 0;
            expect(document.addModulator(wheel, id).wasOk());
            expect(document.addRoute({0, id, clip.id, "position.y", 2, motion::ModulationMode::add}, route).wasOk());
            motion::PreparedComposition prepared(document.project(), 48000, nullptr, motion::CompositionPurpose::signal);
            expect(prepared.preparationError.isEmpty(), prepared.preparationError);
            const auto& item = prepared.clips.front();
            expectWithinAbsoluteError(item.curves[1].evaluate(item.localTime(0.5)), 0.0, 1.0e-12);
            expectWithinAbsoluteError(item.curves[1].evaluate(item.localTime(1.5)), 2.0, 1.0e-12); // CC1 = 127 from beat 2 (1 s)
            expectEquals(static_cast<int>(prepared.liveTracks.size()), 1);
            motion::Project loaded;
            expect(motion::Document::prepareLoad(document.save(), loaded).wasOk());
            expectEquals(loaded.tracks[0].midiInput, motion::Track::anyMidiChannel);
            expect(loaded.tracks[0].clips[0].midi->controls() == clip.midi->controls());

            // After the clip (at 6 s) nothing draws, until a live note arrives.
            motion::LiveMidiInputs inputs;
            inputs.count = 1;
            inputs.routes[0].track = track.id;
            inputs.routes[0].performance.useInstrument(*prepared.liveTracks[0].instrument);
            motion::BeamRenderer renderer;
            const auto lit = [&](const motion::LiveMidiInputs* live) {
                int count = 0;
                for (std::int64_t index = 0; index < 4800; ++index) {
                    const auto point = renderer.sample(prepared, 6.0, 288000 + index, 48000, false, 1, nullptr, live);
                    count += point.r > 0 || point.g > 0 || point.b > 0 ? 1 : 0;
                }
                return count;
            };
            inputs.clockOffset = 0;
            expectEquals(lit(&inputs), 0);
            const unsigned char on[] {0x90, 69, 127};
            expect(inputs.apply(on, 3, 288000, &motion::applyLiveMidi));
            renderer.reset();
            expect(lit(&inputs) > 1000, "a held live note draws the track's clip");
            expectEquals(lit(nullptr), 0);
        }
    }
};

static MotionMidiExpressionTest motionMidiExpressionTest;
