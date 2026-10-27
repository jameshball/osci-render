#include <JuceHeader.h>
#include "../Source/motion/model/AnimateChange.h"
#include "../Source/motion/import/SourceDecoding.h"

// A value changed past a clip's start, where it wasn't animated, becomes an
// animation from its old value at the start to the new one there.
class MotionAnimateChangeTest : public juce::UnitTest {
public:
    MotionAnimateChangeTest() : juce::UnitTest("Motion animate change", "Motion") {}

    void runTest() override {
        beginTest("A change to a value that wasn't animated is found; animated ones and moved clips aren't");
        {
            const auto before = projectWithClip(2);
            auto after = before;
            properties(after)["position.x"] = motion::Curve(0.5);
            auto changes = motion::staticChanges(before, after);
            expectEquals(static_cast<int>(changes.size()), 1);
            expect(changes[0].target == 20 && changes[0].property == "position.x");
            expectEquals(changes[0].before, 0.0);
            expectEquals(changes[0].after, 0.5);
            // Already animated: keying there is the ordinary edit.
            auto keyed = before;
            properties(keyed)["position.y"].setKey({0, 0});
            auto keyedAfter = keyed;
            properties(keyedAfter)["position.y"].setKeyValue(1, 1);
            expect(motion::staticChanges(keyed, keyedAfter).empty());
            // A clip moved with a value changed is a move, not a value change.
            auto moved = after;
            motion::moveClips(moved.tracks, {20}, 1.0, 0, moved.tempo());
            expect(motion::staticChanges(before, moved).empty());
        }

        beginTest("Animating keys the old value at the clip's start and the new one at the playhead");
        {
            const auto before = projectWithClip(2);
            auto after = before;
            properties(after)["position.x"] = motion::Curve(0.5);
            const auto changes = motion::staticChanges(before, after);
            expect(!motion::canAnimateAt(after, changes[0], 2.0), "not at the clip's very start");
            expect(!motion::canAnimateAt(after, changes[0], 1.0), "not before the clip");
            expect(motion::canAnimateAt(after, changes[0], 3.0));
            expect(motion::animateChanges(after, changes, 3.0));
            const auto& curve = properties(after)["position.x"];
            expectEquals(static_cast<int>(curve.keyframes().size()), 2);
            expectEquals(curve.evaluate(0.0), 0.0);
            expectEquals(curve.evaluate(1.0), 0.5);
            expect(curve.keyframes()[0].interpolation == motion::Interpolation::smooth, "eased as keys are by default");
            expect(!motion::animateChanges(after, changes, 3.0), "once animated, there's nothing left to animate");
            // Position's other axes are keyed where they stand, as its diamond keys them all.
            const auto& y = properties(after)["position.y"];
            expectEquals(static_cast<int>(y.keyframes().size()), 2);
            expectEquals(y.evaluate(0.5), 0.0);
        }

        beginTest("A value never stored before counts from its default");
        {
            const auto before = projectWithClip(0);
            auto after = before;
            properties(after).erase("opacity");
            auto stored = after;
            properties(stored)["opacity"] = motion::Curve(40);
            const auto changes = motion::staticChanges(after, stored);
            expectEquals(static_cast<int>(changes.size()), 1);
            expectEquals(changes[0].before, 100.0);
            expect(motion::animateChanges(stored, changes, 2.0));
            expect(properties(stored)["opacity"].keyframes()[0].interpolation == motion::Interpolation::smooth, "opacity's whole numbers still move");
        }
    }

private:
    static motion::PropertyMap& properties(motion::Project& project) { return *motion::findPropertyTarget(project, 20)->properties; }

    static motion::Project projectWithClip(double start) {
        motion::Project project;
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "Hi.txt";
        asset->extension = ".txt";
        asset->data.append("Hi", 2);
        motion::decodeAsset(*asset);
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(20, *asset, start);
        motion::Track track;
        track.id = 10;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
        return project;
    }
};

static MotionAnimateChangeTest motionAnimateChangeTest;
