#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"

class MotionViewStateTest : public juce::UnitTest {
public:
    MotionViewStateTest() : juce::UnitTest("Motion view state", "Motion") {}

    static motion::Project twoTracks(motion::Document& document) {
        motion::Project project;
        project.duration = 20;
        for (const auto* name : {"One", "Two"}) {
            motion::Track track;
            track.id = document.newId();
            track.name = name;
            project.tracks.push_back(track);
        }
        return project;
    }

    void runTest() override {
        beginTest("Track heights change without undo steps and survive undo and redo");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            document.reset(twoTracks(document));
            const auto first = document.project().tracks[0].id;
            const auto revision = document.revision();
            expect(document.setTrackHeight(first, 64));
            expectEquals(document.project().tracks[0].height, 64);
            expect(!undo.canUndo(), "resizing a row is not an edit");
            expectEquals(document.revision(), revision, "no revision bump disturbs open gestures");
            document.edit("Rename", [](motion::Project& project) { project.tracks[1].name = "Renamed"; });
            expect(document.setTrackHeight(first, 90));
            expect(undo.undo());
            expectEquals(juce::String(document.project().tracks[1].name), juce::String("Two"));
            expectEquals(document.project().tracks[0].height, 90, "undo keeps the current height");
            expect(undo.redo());
            expectEquals(document.project().tracks[0].height, 90);
            expect(document.setTrackHeight(first, 5000));
            expectEquals(document.project().tracks[0].height, motion::Track::maximumHeight);
            expect(document.setTrackHeight(first, 1));
            expectEquals(document.project().tracks[0].height, motion::Track::minimumHeight);
            expect(document.setTrackHeight(first, 0));
            expectEquals(document.project().tracks[0].height, 0, "0 is the default height");
            expect(!document.setTrackHeight(999999, 40), "unknown tracks are refused");
        }
        beginTest("Heights and colour labels save, load, and never carry into another document");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            document.reset(twoTracks(document));
            const auto id = document.project().tracks[0].id;
            document.setTrackHeight(id, 48);
            document.edit("Label", [](motion::Project& project) { project.tracks[0].label = 3; });
            motion::Project loaded;
            expect(motion::Document::prepareLoad(document.save(), loaded).wasOk());
            expectEquals(loaded.tracks[0].height, 48);
            expectEquals(loaded.tracks[0].label, 3);
            // A new document with colliding ids keeps its own heights.
            juce::UndoManager otherUndo;
            motion::Document other(otherUndo);
            other.reset(twoTracks(other));
            other.setTrackHeight(other.project().tracks[0].id, 120);
            auto fresh = twoTracks(other);
            fresh.tracks[0].id = other.project().tracks[0].id;
            other.reset(fresh);
            expectEquals(other.project().tracks[0].height, 0, "reset does not inherit view state");
        }
        beginTest("The loop range saves, loads and follows tempo changes in musical projects");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            motion::Project project;
            project.duration = 20;
            project.timeDisplay = motion::TimeDisplay::beats;
            project.loopStart = 2; project.loopEnd = 4; project.looping = true;
            document.reset(project);
            motion::Project loaded;
            expect(motion::Document::prepareLoad(document.save(), loaded).wasOk());
            expect(loaded.hasLoop() && loaded.looping && loaded.loopStart == 2 && loaded.loopEnd == 4);
            expect(document.changeTempo(60).wasOk());
            expectWithinAbsoluteError(document.project().loopStart, 4.0, 1e-9, "the loop keeps its beats");
            expectWithinAbsoluteError(document.project().loopEnd, 8.0, 1e-9);
            motion::Project invalid;
            invalid.loopStart = 5; invalid.loopEnd = 3; invalid.looping = true;
            document.reset(invalid);
            expect(motion::Document::prepareLoad(document.save(), loaded).wasOk());
            expect(!loaded.hasLoop() && !loaded.looping, "an inverted loop is dropped");
        }
    }
};

static MotionViewStateTest motionViewStateTest;
