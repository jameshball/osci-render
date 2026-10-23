#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/model/PropertyTarget.h"

// Undo steps, previews and preparation copy the project; those copies must
// share every track an edit did not change, or undo memory grows with the
// whole project again.
class MotionTrackSharingTest : public juce::UnitTest {
public:
    MotionTrackSharingTest() : juce::UnitTest("Motion track sharing", "Motion") {}

    static motion::Project fourTracks(motion::Document& document) {
        motion::Project project;
        project.duration = 20;
        for (int index = 0; index < 4; ++index) {
            motion::Track track;
            track.id = document.newId();
            track.name = "Track " + std::to_string(index + 1);
            motion::Clip clip;
            clip.id = document.newId();
            clip.start = 1;
            clip.duration = 4;
            clip.properties = motion::defaultProperties(motion::objectPropertySpecs);
            clip.effects.push_back(motion::makeEffect(document.newId(), *motion::effectDefinition("rotate")));
            track.clips.push_back(clip);
            project.tracks.push_back(track);
        }
        return project;
    }

    // Every track but `changed` is the same object in both snapshots.
    void expectSharedExcept(const motion::Project& after, const motion::Project& before, std::size_t changed, const juce::String& edit) {
        expectEquals(static_cast<int>(after.tracks.size()), static_cast<int>(before.tracks.size()));
        for (std::size_t index = 0; index < after.tracks.size(); ++index) {
            expect(after.tracks.shares(index, before.tracks) == (index != changed), edit + ": track " + juce::String(static_cast<int>(index)));
        }
    }

    void runTest() override {
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(fourTracks(document));
        const auto clipOn = [&](std::size_t track) { return document.project().tracks[track].clips[0].id; };

        beginTest("Keying one property copies only its track");
        {
            const auto before = document.project();
            const auto clip = clipOn(1);
            document.edit("Key position", [clip](motion::Project& project) { motion::findPropertyCurve(project, clip, "position.x")->setKeyValue(2, 0.5); });
            expectSharedExcept(document.project(), before, 1, "key");
            expect(undo.undo());
            expectSharedExcept(document.project(), before, 4, "undo restores the very same tracks");
            expect(undo.redo());
        }
        beginTest("Clip timing, effects, track settings and deletion copy only the track they change");
        {
            auto before = document.project();
            auto timing = document.project().tracks[2].clips[0].timing(document.project().tempo());
            timing.moveTo(6);
            expect(document.setClipTiming(clipOn(2), timing).wasOk());
            expectSharedExcept(document.project(), before, 2, "clip timing");

            before = document.project();
            const auto effect = document.project().tracks[3].clips[0].effects[0].id;
            document.edit("Key effect", [effect](motion::Project& project) { motion::findEffect(project, effect)->properties.begin()->second.setKeyValue(1, 0.25); });
            expectSharedExcept(document.project(), before, 3, "effect key");

            before = document.project();
            const auto track = document.project().tracks[0].id;
            document.edit("Lock", [track](motion::Project& project) { project.tracks.changeById(track)->locked = true; });
            expectSharedExcept(document.project(), before, 0, "track lock");

            before = document.project();
            expect(document.removeClips({clipOn(2)}).wasOk());
            expectSharedExcept(document.project(), before, 2, "delete clip");
        }
        beginTest("View state and caches copy only the tracks they touch");
        {
            const auto before = document.project();
            expect(document.setTrackHeight(document.project().tracks[1].id, 80));
            expectSharedExcept(document.project(), before, 1, "track height");
        }
        beginTest("Moving clips between tracks copies only the two tracks involved");
        {
            auto project = document.project();
            const auto before = project;
            expect(motion::moveClips(project.tracks, {project.tracks[1].clips[0].id}, 0, 1, project.tempo()));
            for (std::size_t index = 0; index < project.tracks.size(); ++index) {
                expect(project.tracks.shares(index, before.tracks) == (index != 1 && index != 2), "move: track " + juce::String(static_cast<int>(index)));
            }
        }
    }
};

static MotionTrackSharingTest motionTrackSharingTest;
