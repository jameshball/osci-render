#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/CompositionRenderer.h"

class MotionTempoMapTest : public juce::UnitTest {
public:
    MotionTempoMapTest() : juce::UnitTest("Motion tempo map", "Motion") {}

    static motion::Tempo halfTimeAtBar3() {
        return motion::Tempo(120, std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{8, 60}}));
    }

    void runTest() override {
        beginTest("Beats and seconds convert exactly across a tempo change");
        {
            const auto tempo = halfTimeAtBar3();
            expect(tempo.valid());
            expectEquals(tempo.seconds(8), 4.0);
            expectEquals(tempo.seconds(12), 8.0);
            expectEquals(tempo.beats(6), 10.0);
            expectEquals(tempo.beats(2), 4.0);
            expectEquals(tempo.bpmAt(3), 120.0);
            expectEquals(tempo.bpmAt(5), 60.0);
            expectWithinAbsoluteError(tempo.averageBpm(4, 12), 80.0, 1.0e-12);
            for (const auto seconds : {0.0, 1.5, 4.0, 7.25, 30.0}) { expectWithinAbsoluteError(tempo.seconds(tempo.beats(seconds)), seconds, 1.0e-12); }
            expect(!motion::Tempo(120, std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{8, 60}, {4, 90}})).valid());
            expect(!motion::Tempo(120, std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{0, 60}})).valid());
        }
        beginTest("Musical clips place by the map; content spans at its average tempo");
        {
            const auto tempo = halfTimeAtBar3();
            motion::Clip clip;
            clip.id = 1; clip.timeBase = motion::ClipTimeBase::beats; clip.contentBpm = 120;
            clip.start = 8; clip.duration = 4;
            const auto after = clip.timing(tempo);
            expectEquals(after.start, 4.0);
            expectEquals(after.end(), 8.0);
            expectWithinAbsoluteError(after.rate, 0.5, 1.0e-12); // content authored at 120 plays at 60
            clip.start = 4; clip.duration = 8;
            const auto spanning = clip.timing(tempo);
            expectEquals(spanning.start, 2.0);
            expectEquals(spanning.end(), 8.0);
            expectWithinAbsoluteError(spanning.rate, 80.0 / 120.0, 1.0e-12);
            auto moved = clip;
            auto timing = moved.timing(tempo);
            timing.moveTo(4);
            expect(moved.setTiming(timing, tempo));
            expectEquals(moved.start, 8.0);
            expectWithinAbsoluteError(moved.timing(tempo).start, 4.0, 1.0e-12);
        }
        beginTest("The grid labels and snaps in beats that follow the map");
        {
            motion::TimeGrid grid;
            grid.display = motion::TimeDisplay::beats;
            grid.bpm = 120;
            grid.tempoChanges = std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{8, 60}});
            grid.snapBeats = 1;
            expectEquals(juce::String(grid.positionLabel(6)), juce::String("3.3.000")); // beat 10
            expectWithinAbsoluteError(grid.snap(6.4), 6.0, 1.0e-12);
            expectWithinAbsoluteError(grid.snap(6.6), 7.0, 1.0e-12);
            const auto parsed = grid.parsePosition("3.3");
            expect(parsed.has_value() && std::abs(*parsed - 6.0) < 1.0e-12);
        }
        beginTest("Tempo changes save, reload, move musical clips and refuse overlaps");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            auto asset = std::make_shared<motion::Asset>();
            asset->id = document.newId(); asset->name = "point.obj"; asset->extension = ".obj";
            const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
            motion::Document::decodeAsset(*asset);
            auto musical = motion::Document::makeClip(document.newId(), *asset, 0);
            musical.timeBase = motion::ClipTimeBase::beats; musical.contentBpm = 120; musical.start = 8; musical.duration = 4;
            auto fixed = motion::Document::makeClip(document.newId(), *asset, 0);
            fixed.start = 7; fixed.duration = 1;
            motion::Track a, b;
            a.id = document.newId(); a.name = "Beats"; a.insert(musical, motion::Tempo(120));
            b.id = document.newId(); b.name = "Seconds"; b.insert(fixed, motion::Tempo(120));
            motion::Project project;
            project.duration = 20; project.assets = {asset}; project.tracks = {a, b};
            document.reset(project);
            expect(document.setTempoChange(0, 90).failed(), "the initial tempo is the project tempo");
            expect(document.setTempoChange(8, 60).wasOk());
            expect(document.project().tracks[0].clips[0].timing(document.project().tempo()).start == 4.0);
            expect(document.project().tracks[1].clips[0].timing(document.project().tempo()).start == 7.0, "seconds clips stay put");
            motion::Project loaded;
            const auto result = motion::Document::prepareLoad(document.save(), loaded);
            expect(result.wasOk(), result.getErrorMessage());
            expect(loaded.tempoChanges != nullptr && *loaded.tempoChanges == *document.project().tempoChanges);
            motion::PreparedComposition composition(document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            expect(composition.clips.size() == 2 && composition.clips[0].start == 4.0 && composition.clips[0].end == 8.0);
            expect(document.removeTempoChange(8).wasOk());
            expect(document.project().tempoChanges == nullptr);
            expect(undo.undo());
            expect(document.project().tempoChanges != nullptr);
            // Slowing a musical clip into a seconds clip on its track is refused.
            auto crowded = document.project();
            crowded.tempoChanges.reset();
            auto pinned = motion::Document::makeClip(document.newId(), *asset, 0);
            pinned.start = 6.5; pinned.duration = 0.5;
            expect(crowded.tracks[0].insert(pinned, crowded.tempo()));
            document.reset(crowded);
            expect(document.setTempoChange(8, 60).failed());
            expect(document.project().tempoChanges == nullptr);
            expect(document.setTempoChange(10, 200).wasOk(), "a change after the clip's start that does not overlap is fine");
        }
        beginTest("Splits, trims and moves across tempo changes keep content continuous");
        {
            const auto tempo = halfTimeAtBar3();
            motion::Clip clip;
            clip.id = 1; clip.timeBase = motion::ClipTimeBase::beats; clip.contentBpm = 120;
            clip.start = 4; clip.duration = 8; // 2 s .. 8 s, across the change at beat 8
            const auto whole = clip.timing(tempo);
            const auto parts = clip.split(4.0, 2, tempo); // at beat 8
            expect(parts.has_value());
            for (const auto time : {2.5, 3.9, 4.0, 5.0, 7.5}) {
                const auto& half = time < 4.0 ? parts->first : parts->second;
                expectWithinAbsoluteError(half.timing(tempo).localTime(time), whole.localTime(time), 1.0e-9);
            }
            auto trimmed = clip;
            expect(trimmed.trim(3.0, 8.0, tempo));
            expectWithinAbsoluteError(trimmed.timing(tempo).localTime(6.0), whole.localTime(6.0), 1.0e-9);
            // A pure move keeps the clip's length in beats (its bars).
            motion::Clip short_;
            short_.id = 3; short_.timeBase = motion::ClipTimeBase::beats; short_.contentBpm = 120; short_.start = 0; short_.duration = 4;
            auto timing = short_.timing(tempo);
            timing.moveTo(tempo.seconds(12));
            expect(short_.setTiming(timing, tempo));
            expectWithinAbsoluteError(short_.duration, 4.0, 1.0e-9);
            expectWithinAbsoluteError(short_.start, 12.0, 1.0e-9);
        }
        beginTest("Tempo changes keep markers on their beats; compositions keep the map; synced LFOs follow it");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            motion::Project project;
            project.duration = 20;
            project.timeDisplay = motion::TimeDisplay::beats;
            project.markers.push_back({document.newId(), 6, "Bar four"}); // beat 12 at 120
            motion::Group group;
            group.id = document.newId();
            group.properties["position.x"].modulation = {true, motion::ModulationWaveform::saw, 1, 1, 0, true, 4};
            project.groups.push_back(group);
            document.reset(project);
            expect(document.setTempoChange(8, 60).wasOk());
            const auto tempo = document.project().tempo();
            expectWithinAbsoluteError(tempo.beats(document.project().markers[0].time), 12.0, 1.0e-9);
            motion::PreparedComposition prepared(document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            juce::ignoreUnused(prepared);
            // A group LFO of 4 beats: at beat 10 (6 s under the map) it is halfway: saw = 0.
            motion::PreparedGroup preparedGroup(document.project().groups[0]);
            motion::PreparedDrivers drivers(nullptr);
            drivers.drive(preparedGroup.curves[0], document.project(), motion::ClipTiming(0, 20), group.id, "position.x");
            expectWithinAbsoluteError(preparedGroup.curves[0].evaluate(tempo.seconds(10)), 0.0, 1.0e-9);
            expectWithinAbsoluteError(preparedGroup.curves[0].evaluate(tempo.seconds(11)), 0.5, 1.0e-9);
        }
        beginTest("Changing the initial tempo keeps project-time items on their beats");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            motion::Project project;
            project.duration = 20;
            project.timeDisplay = motion::TimeDisplay::beats;
            project.tempoChanges = std::make_shared<const std::vector<motion::TempoChange>>(std::vector<motion::TempoChange> {{8, 60}});
            project.markers.push_back({document.newId(), 6, "Beat ten"});
            document.reset(project);
            expect(document.changeTempo(240).wasOk());
            const auto tempo = document.project().tempo();
            expectWithinAbsoluteError(tempo.beats(document.project().markers[0].time), 10.0, 1.0e-9);
        }
    }
};

static MotionTempoMapTest motionTempoMapTest;
