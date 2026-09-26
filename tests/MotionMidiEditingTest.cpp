#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"

class MotionMidiEditingTest : public juce::UnitTest {
public:
    MotionMidiEditingTest() : juce::UnitTest("Motion MIDI authoring commands", "MotionMidi") {}

    void runTest() override {
        juce::UndoManager undo;
        motion::Document document(undo);
        auto geometry = std::make_shared<motion::Asset>();
        geometry->id = document.newId(); geometry->name = "triangle.obj"; geometry->extension = ".obj";
        const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        geometry->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
        auto midi = std::make_shared<motion::Asset>();
        midi->id = document.newId(); midi->name = "notes.mid"; midi->extension = ".mid";
        const unsigned char bytes[] { 'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,
            'M','T','r','k',0,0,0,13,0,0x90,60,100,0x83,0x60,0x80,60,0,0,0xff,0x2f,0 };
        midi->data.append(bytes, sizeof(bytes));
        beginTest("Assignment preserves geometry, resolved timing and curve keys");
        expect(motion::Document::decodeAsset(*geometry).wasOk());
        const auto decoded = motion::Document::decodeAsset(*midi);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        if (decoded.failed()) { return; }
        auto clip = motion::Document::makeClip(document.newId(), *geometry, 2);
        clip.duration = 3; clip.offset = .5; clip.rate = 1.25;
        clip.properties["position.x"].setKey({.75, 2, motion::Interpolation::smooth});
        auto sibling = clip; sibling.id = document.newId(); sibling.start = 8;
        motion::Track track; track.id = document.newId(); track.name = "Instrument";
        expect(track.insert(clip)); expect(track.insert(sibling));
        motion::Project initial; initial.bpm = 150; initial.assets = {geometry, midi}; initial.tracks = {track};
        document.reset(initial);
        const auto current = [&]() -> const motion::Clip& { return document.project().tracks[0].clips[0]; };
        const auto timing = clip.timing(initial.bpm);
        expect(document.assignMidi(clip.id, midi->id).wasOk());
        expect(current().midi == midi->midi && current().midiAsset == midi->id);
        expect(current().asset == geometry->id && current().timeBase == motion::ClipTimeBase::beats);
        expectEquals(current().contentBpm, 150.0);
        expectWithinAbsoluteError(current().timing(150).start, timing.start, 1e-12);
        expectWithinAbsoluteError(current().timing(150).end(), timing.end(), 1e-12);
        expectWithinAbsoluteError(current().timing(150).offset, timing.offset, 1e-12);
        expectWithinAbsoluteError(current().timing(150).rate, timing.rate, 1e-12);
        expectEquals(current().properties.at("position.x").evaluate(.75), 2.0);
        expect(undo.undo());
        expect(current().midi == nullptr && current().timeBase == motion::ClipTimeBase::seconds);
        expect(undo.redo());
        expect(current().midi == midi->midi);

        beginTest("Immutable edits are independent and undo restores exact pattern pointers");
        expect(document.assignMidi(sibling.id, midi->id).wasOk());
        auto note = midi->midi->notes()[0]; note.pitch = 72;
        const auto changed = motion::MidiNotes::create({note});
        expect(static_cast<bool>(changed));
        expect(document.setMidiNotes(clip.id, changed.source, "Transpose notes").wasOk());
        expect(current().midi == changed.source && current().midiAsset == midi->id);
        expect(document.project().tracks[0].clips[1].midi == midi->midi);
        expect(undo.undo()); expect(current().midi == midi->midi);
        expect(undo.redo()); expect(current().midi == changed.source);
        const auto assigned = current();
        expect(document.clearMidi(clip.id).wasOk());
        expect(current().midi == nullptr && current().midiAsset == 0);
        expect(current().timeBase == assigned.timeBase && current().contentBpm == assigned.contentBpm);
        expect(current().start == assigned.start && current().duration == assigned.duration
            && current().offset == assigned.offset && current().rate == assigned.rate);
        expectEquals(current().properties.at("position.x").evaluate(.75), 2.0);
        expect(undo.undo()); expect(current().midi == changed.source && current().midiAsset == midi->id);
        expect(undo.redo()); expect(current().midi == nullptr);

        beginTest("No-op commands and invalid targets leave revision and undo history unchanged");
        auto revision = document.revision();
        const auto undoDescription = undo.getUndoDescription();
        expect(document.clearMidi(clip.id).wasOk());
        expect(document.setMidiNotes(clip.id, changed.source, "Invalid edit").failed());
        expect(document.setMidiNotes(clip.id, nullptr, "Invalid edit").failed());
        expect(document.assignMidi(clip.id, geometry->id).failed());
        expect(document.assignMidi(clip.id, 999999).failed());
        expect(document.assignMidi(999999, midi->id).failed());
        expect(document.clearMidi(999999).failed());
        expect(document.revision() == revision && undo.getUndoDescription() == undoDescription);

        beginTest("Empty authored patterns retain timing and support subsequent edits");
        expect(document.assignMidi(clip.id, 0).wasOk());
        expect(current().midi != nullptr && current().midi->notes().empty() && current().midiAsset == 0);
        expect(current().start == assigned.start && current().duration == assigned.duration && current().offset == assigned.offset);
        expect(document.setMidiNotes(clip.id, changed.source, "Draw note").wasOk());
        revision = document.revision();
        expect(document.setMidiNotes(clip.id, changed.source, "Same notes").wasOk());
        expect(document.revision() == revision);
        const auto identical = motion::MidiNotes::create(changed.source->notes());
        expect(identical.source != changed.source);
        expect(document.setMidiNotes(clip.id, identical.source, "Identical notes").wasOk());
        expect(document.revision() == revision && current().midi == changed.source);
        expect(undo.undo()); expect(current().midi->notes().empty());
        expect(undo.redo()); expect(current().midi == changed.source);

        beginTest("Locked and audio tracks reject every authoring command atomically");
        for (const bool audio : {false, true}) {
            auto blocked = document.project();
            blocked.tracks[0].locked = !audio;
            blocked.tracks[0].kind = audio ? motion::TrackKind::audio : motion::TrackKind::visual;
            document.reset(blocked);
            revision = document.revision();
            expect(document.assignMidi(clip.id, midi->id).failed());
            expect(document.assignMidi(clip.id, 0).failed());
            expect(document.setMidiNotes(clip.id, midi->midi, "Blocked edit").failed());
            expect(document.clearMidi(clip.id).failed());
            expect(document.revision() == revision && current().midi == changed.source && !undo.canUndo());
        }

        beginTest("Undecoded MIDI assets cannot be assigned");
        auto invalid = initial;
        auto undecoded = std::make_shared<motion::Asset>();
        undecoded->id = document.newId(); undecoded->extension = ".mid";
        invalid.assets.push_back(undecoded);
        document.reset(invalid);
        revision = document.revision();
        expect(document.assignMidi(clip.id, undecoded->id).failed());
        expect(document.revision() == revision && !undo.canUndo());

        beginTest("MIDI assignment preserves exact-touching fractional clip boundaries");
        for (const double bpm : {150.0, 170.0}) {
            auto adjacent = initial;
            adjacent.bpm = bpm;
            auto& first = adjacent.tracks[0].clips[0];
            first.start = bpm == 150 ? .3 : .1; first.duration = first.start;
            const auto before = first.timing(bpm);
            adjacent.tracks[0].clips[1].start = before.end();
            document.reset(adjacent);
            expect(document.assignMidi(clip.id, midi->id).wasOk());
            expect(current().timing(bpm).start >= before.start && current().timing(bpm).end() <= before.end());
            expectWithinAbsoluteError(current().timing(bpm).end(), before.end(), 1e-15);
            expect(undo.undo()); expect(current().timeBase == motion::ClipTimeBase::seconds);
            expect(undo.redo()); expect(current().midi == midi->midi);
        }

        beginTest("Timing commands preserve content, sort tracks and undo in seconds and beats");
        for (const bool beats : {false, true}) {
            for (const bool audio : {false, true}) {
                auto timingProject = initial;
                timingProject.duration = 12;
                timingProject.tracks[0].kind = audio ? motion::TrackKind::audio : motion::TrackKind::visual;
                auto& authored = timingProject.tracks[0].clips[0];
                authored.midi = audio ? nullptr : midi->midi;
                authored.midiAsset = audio ? 0 : midi->id;
                if (beats) { expect(authored.anchorToBeats(timingProject.bpm)); }
                document.reset(timingProject);
                const auto findClip = [&]() -> const motion::Clip& {
                    const auto& clips = document.project().tracks[0].clips;
                    return *std::find_if(clips.begin(), clips.end(), [&](const auto& candidate) { return candidate.id == clip.id; });
                };
                const auto original = findClip();
                const auto originalTiming = original.timing(timingProject.bpm);
                revision = document.revision();
                expect(document.setClipTiming(clip.id, originalTiming).wasOk());
                expect(document.revision() == revision && !undo.canUndo());
                auto moved = originalTiming; moved.moveTo(20);
                expect(document.setClipTiming(clip.id, moved).wasOk());
                expect(document.project().tracks[0].clips.back().id == clip.id);
                expectWithinAbsoluteError(findClip().timing(timingProject.bpm).start, 20.0, 1e-12);
                expectWithinAbsoluteError(findClip().timing(timingProject.bpm).duration(), originalTiming.duration(), 1e-12);
                expectEquals(findClip().offset, original.offset);
                expectEquals(findClip().rate, original.rate);
                expectEquals(document.project().duration, findClip().timing(timingProject.bpm).end());
                expect(findClip().asset == original.asset && findClip().midi == original.midi && findClip().midiAsset == original.midiAsset);
                expect(findClip().timeBase == original.timeBase && findClip().contentBpm == original.contentBpm);
                expectEquals(findClip().properties.at("position.x").evaluate(.75), 2.0);
                expect(document.project().assets[0] == geometry);
                expect(undo.undo()); expectEquals(findClip().start, original.start);
                expectEquals(document.project().duration, 12.0);
                expect(undo.redo()); expect(document.project().tracks[0].clips.back().id == clip.id);

                auto edited = findClip().timing(timingProject.bpm);
                edited.setDuration(1.5);
                expect(document.setClipTiming(clip.id, edited).wasOk());
                expectWithinAbsoluteError(findClip().timing(timingProject.bpm).duration(), 1.5, 1e-12);
                expectEquals(findClip().rate, original.rate);
                expectEquals(findClip().offset, original.offset);
                edited = findClip().timing(timingProject.bpm); edited.offset = -1.25;
                expect(document.setClipTiming(clip.id, edited).wasOk());
                expectWithinAbsoluteError(findClip().timing(timingProject.bpm).offset, -1.25, 1e-12);
                edited = findClip().timing(timingProject.bpm); edited.rate = 2.5;
                expect(document.setClipTiming(clip.id, edited).wasOk());
                expectWithinAbsoluteError(findClip().timing(timingProject.bpm).rate, 2.5, 1e-12);
                expectWithinAbsoluteError(findClip().timing(timingProject.bpm).duration(), 1.5, 1e-12);
                expect(undo.undo()); expectEquals(findClip().rate, original.rate);
                expect(undo.undo()); expectEquals(findClip().offset, original.offset);
                expect(undo.undo()); expectEquals(findClip().duration, original.duration);

                beginTest("Invalid, overlapping and locked timing requests leave document and undo untouched");
                const auto stable = findClip().timing(timingProject.bpm);
                revision = document.revision();
                const auto undoName = undo.getUndoDescription();
                auto overlap = stable; overlap.moveTo(8.5);
                expect(document.setClipTiming(clip.id, overlap).failed());
                auto invalidTiming = stable; invalidTiming.rate = std::numeric_limits<double>::infinity();
                expect(document.setClipTiming(clip.id, invalidTiming).failed());
                invalidTiming = stable; invalidTiming.offset = std::numeric_limits<double>::quiet_NaN();
                expect(document.setClipTiming(clip.id, invalidTiming).failed());
                invalidTiming = stable; invalidTiming.setDuration(0);
                expect(document.setClipTiming(clip.id, invalidTiming).failed());
                invalidTiming = stable; invalidTiming.moveTo(-1);
                expect(document.setClipTiming(clip.id, invalidTiming).failed());
                expect(document.setClipTiming(999999, stable).failed());
                expect(document.revision() == revision && undo.getUndoDescription() == undoName);
                auto locked = document.project(); locked.tracks[0].locked = true; document.reset(locked);
                revision = document.revision();
                expect(document.setClipTiming(clip.id, originalTiming).failed());
                expect(document.revision() == revision && !undo.canUndo());
            }
        }
        beginTest("Beat timing converts resolved speed and offset at a different project tempo");
        auto slower = initial;
        expect(slower.tracks[0].clips[0].anchorToBeats(150));
        slower.bpm = 75; slower.tracks[0].clips[1].start = 20;
        document.reset(slower);
        auto slowTiming = current().timing(75);
        slowTiming.moveTo(12); slowTiming.offset = 2; slowTiming.rate = .75;
        expect(document.setClipTiming(clip.id, slowTiming).wasOk());
        expectWithinAbsoluteError(current().timing(75).start, 12.0, 1e-12);
        expectWithinAbsoluteError(current().timing(75).duration(), 6.0, 1e-12);
        expectWithinAbsoluteError(current().timing(75).offset, 2.0, 1e-12);
        expectWithinAbsoluteError(current().timing(75).rate, .75, 1e-12);
        expectEquals(current().contentBpm, 150.0);
        expect(undo.undo()); expectEquals(current().start, slower.tracks[0].clips[0].start);
        expect(undo.redo()); expectWithinAbsoluteError(current().timing(75).rate, .75, 1e-12);
        testDuplication(initial);
    }

private:
    void testDuplication(const motion::Project& initial) {
        beginTest("Adjacent duplication preserves content with fresh clip and effect identities");
        for (const bool beats : {false, true}) {
            for (const bool audio : {false, true}) {
                juce::UndoManager undo;
                motion::Document document(undo);
                auto project = initial;
                project.tracks[0].clips.resize(1);
                project.tracks[0].kind = audio ? motion::TrackKind::audio : motion::TrackKind::visual;
                auto& clip = project.tracks[0].clips[0];
                const auto pattern = motion::MidiNotes::create({{1, 0, 1, 60, 100, 1}}).source;
                if (!audio) {
                    clip.midi = pattern;
                    clip.effects.push_back(motion::makeEffect(100, *motion::effectDefinition("rotate")));
                    clip.effects.back().properties["rotateZ"].setKey({1, .5, motion::Interpolation::linear});
                }
                if (beats) { expect(clip.anchorToBeats(project.bpm)); project.bpm = 75; }
                project.duration = clip.timing(project.bpm).end();
                const auto original = clip;
                const auto duration = project.duration;
                document.reset(project);
                motion::Id duplicate = 0;
                expect(document.duplicateClip(original.id, duplicate).wasOk());
                expect(duplicate != 0 && duplicate != original.id);
                const auto& copied = document.project().tracks[0].clips.back();
                expect(copied.id == duplicate && copied.start == original.end());
                expect(copied.timeBase == original.timeBase && copied.duration == original.duration && copied.offset == original.offset && copied.rate == original.rate);
                expect(copied.asset == original.asset && copied.midi == original.midi && copied.midiAsset == original.midiAsset);
                expectEquals(copied.properties.at("position.x").evaluate(.75), 2.0);
                expectEquals(document.project().duration, copied.timing(project.bpm).end());
                expect(document.project().assets[0] == initial.assets[0]);
                if (!audio) {
                    expect(copied.effects[0].id != original.effects[0].id && copied.effects[0].id != copied.id);
                    expectEquals(copied.effects[0].properties.at("rotateZ").evaluate(1), .5);
                }
                expect(undo.undo());
                expect(document.project().tracks[0].clips.size() == 1 && document.project().duration == duration);
                expect(undo.redo());
                expect(document.project().tracks[0].clips.back().id == duplicate);
                if (!audio) {
                    auto note = pattern->notes()[0]; note.pitch = 72;
                    const auto changed = motion::MidiNotes::create({note}).source;
                    expect(document.setMidiNotes(duplicate, changed, "Edit duplicate").wasOk());
                    expect(document.project().tracks[0].clips.front().midi == pattern);
                    expect(document.project().tracks[0].clips.back().midi == changed);
                    expect(undo.undo()); expect(document.project().tracks[0].clips.back().midi == pattern);
                }
            }
        }
        beginTest("Rejected duplicates do not alter the document or consume identities");
        for (int reason = 0; reason < 4; ++reason) {
            juce::UndoManager undo;
            motion::Document document(undo);
            auto project = initial;
            const auto source = project.tracks[0].clips[0].id;
            if (reason == 0) { project.tracks[0].clips[1].start = project.tracks[0].clips[0].end(); }
            if (reason == 1) { project.tracks[0].locked = true; }
            if (reason == 2) { project.tracks[0].clips[0].duration = std::numeric_limits<double>::infinity(); }
            document.reset(project);
            const auto marker = document.newId();
            const auto revision = document.revision();
            motion::Id duplicate = 999;
            expect(document.duplicateClip(reason == 3 ? 999999 : source, duplicate).failed());
            expect(duplicate == 0 && document.revision() == revision && !undo.canUndo());
            expect(document.newId() == marker + 1);
            expect(document.project().tracks[0].clips.size() == 2);
        }
        beginTest("Identity exhaustion rejects duplication without wrapping IDs");
        juce::UndoManager undo;
        motion::Document document(undo);
        auto project = initial;
        project.tracks[0].clips.resize(1);
        auto& clip = project.tracks[0].clips[0];
        clip.effects.push_back(motion::makeEffect(std::numeric_limits<motion::Id>::max() - 1, *motion::effectDefinition("rotate")));
        document.reset(project);
        const auto revision = document.revision();
        motion::Id duplicate = 999;
        expect(document.duplicateClip(clip.id, duplicate).failed());
        expect(duplicate == 0 && document.revision() == revision && !undo.canUndo());
        expect(document.newId() == std::numeric_limits<motion::Id>::max());
    }
};

static MotionMidiEditingTest motionMidiEditingTest;
