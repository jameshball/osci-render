#include <JuceHeader.h>
#include "../Source/motion/model/AnchorEdit.h"
#include "../Source/motion/render/CompositionRenderer.h"
#include "../Source/motion/import/SourceDecoding.h"

// The anchor is the point rotation and scale turn about and that lands on
// the position; moving it with its position keeps the object in place.
class MotionAnchorTest : public juce::UnitTest {
public:
    MotionAnchorTest() : juce::UnitTest("Motion anchor", "Motion") {}

    void runTest() override {
        testPose();
        testShiftKeepsPlace();
        testOlderProjects();
    }

private:
    using Curves = std::array<motion::Curve, motion::objectPropertySpecs.size()>;

    static Curves curvesOf(const motion::PropertyMap& properties) {
        Curves curves;
        for (std::size_t index = 0; index < curves.size(); ++index) {
            const auto found = properties.find(std::string(motion::objectPropertySpecs[index].id));
            curves[index] = found != properties.end() ? found->second : motion::Curve(motion::objectPropertySpecs[index].defaultValue);
        }
        return curves;
    }

    void expectNear(osci::Point a, osci::Point b, const juce::String& what) {
        expect(std::abs(a.x - b.x) < 1.0e-4f && std::abs(a.y - b.y) < 1.0e-4f && std::abs(a.z - b.z) < 1.0e-4f,
               what + ": " + juce::String(a.x) + "," + juce::String(a.y) + "," + juce::String(a.z) + " vs " + juce::String(b.x) + "," + juce::String(b.y) + "," + juce::String(b.z));
    }

    void testPose() {
        beginTest("Rotation and scale turn about the anchor, which lands on the position");
        motion::PropertyMap properties;
        properties["position.x"] = motion::Curve(2);
        properties["position.y"] = motion::Curve(1);
        properties["rotation.z"] = motion::Curve(90);
        properties["scale.x"] = motion::Curve(2);
        properties["scale.y"] = motion::Curve(2);
        properties["anchor.x"] = motion::Curve(1);
        const auto pose = motion::TransformPose::at(curvesOf(properties), 0, nullptr);
        expectNear(pose.apply(osci::Point(1, 0, 0), false), osci::Point(2, 1, 0), "the anchor lands on the position");
        expectNear(pose.apply(osci::Point(2, 0, 0), false), osci::Point(2, 3, 0), "a point beside it turns and scales about it");
    }

    static motion::Project projectWithClip(motion::Clip& clip) {
        motion::Project project;
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "Hi.txt";
        asset->extension = ".txt";
        asset->data.append("Hi", 2);
        motion::decodeAsset(*asset);
        project.assets.push_back(asset);
        clip = motion::Document::makeClip(20, *asset, 0);
        motion::Track track;
        track.id = 10;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
        return project;
    }

    void testShiftKeepsPlace() {
        beginTest("Moving the anchor with its position keeps the object in place at every key");
        motion::Clip clip;
        auto project = projectWithClip(clip);
        auto* properties = motion::findPropertyTarget(project, 20)->properties;
        (*properties)["rotation.z"] = motion::Curve(30);
        (*properties)["scale.x"] = motion::Curve(1.5);
        (*properties)["position.x"].setKey({0, -1, motion::Interpolation::linear});
        (*properties)["position.x"].setKey({2, 1, motion::Interpolation::linear});
        const auto before = curvesOf(*properties);
        const motion::Vec3 local {.4, -.2, .1};
        const auto pose = motion::TransformPose::at(before, 1, nullptr);
        const auto moved = pose.apply(osci::Point(.4f, -.2f, .1f), false), origin = pose.apply(osci::Point(0, 0, 0), false);
        const motion::Vec3 parent {moved.x - origin.x, moved.y - origin.y, moved.z - origin.z};
        expect(motion::anchor::shift(project, 20, 1, parent, local));
        const auto after = curvesOf(*motion::findPropertyTarget(project, 20)->properties);
        for (const auto time : {0.0, 1.0, 2.0}) {
            for (const auto& point : {osci::Point(0, 0, 0), osci::Point(.5f, .25f, 0), osci::Point(-.3f, .8f, .2f)}) {
                expectNear(motion::TransformPose::at(after, time, nullptr).apply(point, false), motion::TransformPose::at(before, time, nullptr).apply(point, false), "unchanged at " + juce::String(time));
            }
        }
        expectWithinAbsoluteError(after[motion::anchorIndex].base, .4, 1.0e-9);
        expect(after[0].keyframes().size() == 2, "position keys move, none are added");
        expect(!motion::anchor::shift(project, 20, 1, {std::numeric_limits<double>::infinity(), 0, 0}, local), "a non-finite move changes nothing");
    }

    void testOlderProjects() {
        beginTest("Projects saved before the anchor get one at its default when it is edited, and keep it when saved");
        motion::Clip clip;
        auto project = projectWithClip(clip);
        auto* properties = motion::findPropertyTarget(project, 20)->properties;
        for (const auto* axis : {"anchor.x", "anchor.y", "anchor.z"}) { properties->erase(axis); }
        auto* curve = motion::ensurePropertyCurve(project, 20, "anchor.y");
        expect(curve != nullptr && curve->base == 0, "the missing anchor is made at its default");
        expect(motion::ensurePropertyCurve(project, 20, "no.such") == nullptr, "unknown properties are not made");
        curve->base = .25;
        juce::UndoManager undo, restoreUndo;
        motion::Document document(undo), restored(restoreUndo);
        document.reset(project);
        expect(restored.load(document.save()).wasOk(), "a project with an anchor reopens");
        const auto* reopened = motion::findPropertyCurve(restored.project(), 20, "anchor.y");
        expect(reopened != nullptr && std::abs(reopened->base - .25) < 1.0e-9);
    }
};

static MotionAnchorTest motionAnchorTest;
