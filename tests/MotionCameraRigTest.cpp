#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/CompositionRenderer.h"

class MotionCameraRigTest : public juce::UnitTest {
public:
    MotionCameraRigTest() : juce::UnitTest("Motion camera track, look-at and parenting", "Motion") {}

    struct Fixture {
        juce::UndoManager undo;
        motion::Document document{undo};
        motion::Id clip = 0, group = 0, first = 0, second = 0;
        void initialise() {
            auto asset = std::make_shared<motion::Asset>();
            asset->id = document.newId(); asset->name = "point.obj"; asset->extension = ".obj";
            const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
            motion::decodeAsset(*asset);
            auto item = motion::Document::makeClip(document.newId(), *asset, 0);
            item.duration = 10;
            item.properties["position.x"].base = 2;
            clip = item.id;
            motion::Group parent;
            parent.id = group = document.newId();
            parent.properties["position.x"].base = 1;
            motion::Track track;
            track.id = document.newId(); track.name = "Object"; track.insert(item, motion::Tempo(120));
            motion::Camera a, b;
            a.id = first = document.newId(); a.name = "Front";
            b.id = second = document.newId(); b.name = "Side";
            motion::Project project;
            project.duration = 10; project.assets = {asset}; project.tracks = {track}; project.groups = {parent}; project.cameras = {a, b};
            document.reset(std::move(project));
        }
        motion::PreparedComposition prepare() const { return motion::PreparedComposition(document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry); }
    };

    void runTest() override {
        beginTest("A camera aimed at an object centres it, with Z rotation as roll");
        {
            Fixture f; f.initialise();
            expect(f.document.setCameraRig(f.first, f.clip, 0).wasOk());
            auto composition = f.prepare();
            const auto centre = composition.projectPoint({2, 0, 0}, 1);
            expectWithinAbsoluteError(centre.x, 0.0f, 1.0e-5f);
            expectWithinAbsoluteError(centre.y, 0.0f, 1.0e-5f);
            const auto above = composition.projectPoint({2, 0.5f, 0}, 1);
            expect(above.y > 0.05f && std::abs(above.x) < 1.0e-4f);
            f.document.edit("Roll", [&](motion::Project& project) { project.cameras[0].properties["rotation.z"].base = 90; });
            auto rolled = f.prepare();
            const auto turned = rolled.projectPoint({2, 0.5f, 0}, 1);
            expect(std::abs(turned.x) > 0.05f && std::abs(turned.y) < 1.0e-4f, "roll turns the image about the view axis");
        }
        beginTest("A parented camera moves with its group");
        {
            Fixture f; f.initialise();
            const auto free = f.prepare().projectPoint({0, 0, 0}, 1);
            expectWithinAbsoluteError(free.x, 0.0f, 1.0e-6f);
            expect(f.document.setCameraRig(f.first, 0, f.group).wasOk());
            const auto carried = f.prepare().projectPoint({0, 0, 0}, 1);
            // Moving the camera 1 unit right shifts the world origin left.
            expect(carried.x < -0.05f);
            f.document.edit("Move camera", [&](motion::Project& project) { project.cameras[0].properties["position.x"].base = 1; });
            f.document.setCameraRig(f.first, 0, 0);
            const auto equivalent = f.prepare().projectPoint({0, 0, 0}, 1);
            f.document.edit("Reset camera", [&](motion::Project& project) { project.cameras[0].properties["position.x"].base = 0; });
            expectWithinAbsoluteError(carried.x, equivalent.x, 1.0e-5f);
            expect(f.document.setCameraRig(f.first, 0, f.clip).failed(), "only groups can parent a camera");
        }
        beginTest("Camera cuts split, move, trim, change camera and delete without overlap");
        {
            Fixture f; f.initialise();
            motion::Id cut = 0, later = 0;
            expect(f.document.cutToCamera(f.second, 2, cut).wasOk());
            expect(f.document.cutToCamera(f.first, 6, later).wasOk());
            const auto& cuts = f.document.project().cameraCuts;
            expectEquals(static_cast<int>(cuts.size()), 2);
            expect(cuts[0].start == 2 && cuts[0].end() == 6 && cuts[1].start == 6 && cuts[1].end() == 10);
            expect(f.document.setCutRange(cut, 1, 6.5).failed(), "cuts cannot overlap");
            expect(f.document.setCutRange(cut, 1, 5).wasOk());
            expect(f.document.project().cameraCuts[0].start == 1 && f.document.project().cameraCuts[0].end() == 5);
            expect(f.document.setCutRange(cut, 1, 1.01).failed(), "at least one frame");
            expect(f.document.setCutCamera(cut, f.first).wasOk());
            expect(f.document.project().cameraCuts[0].camera == f.first);
            expect(f.document.removeCut(later).wasOk());
            expectEquals(static_cast<int>(f.document.project().cameraCuts.size()), 1);
            expect(f.undo.undo());
            expectEquals(static_cast<int>(f.document.project().cameraCuts.size()), 2);
        }
        beginTest("Adding a camera frames like the shot it takes over, as one undo step");
        {
            Fixture f; f.initialise();
            f.document.edit("Frame side", [&](motion::Project& project) {
                project.cameras[1].properties["position.x"].base = .5;
                project.cameras[1].properties["fov"].base = 40;
            });
            motion::Id cut = 0;
            expect(f.document.cutToCamera(f.second, 2, cut).wasOk());
            const auto before = f.prepare().projectPoint({.3f, .2f, 0}, 4);
            motion::Id added = 0;
            expect(f.document.addCamera(4, added).wasOk());
            const auto& project = f.document.project();
            expectEquals(static_cast<int>(project.cameras.size()), 3);
            expect(project.cameras.back().id == added && project.cameras.back().name == "Camera 3");
            // The new camera takes over from the playhead to the end of the shot.
            expect(project.cameraCuts.back().camera == added && project.cameraCuts.back().start == 4 && project.cameraCuts.back().end() == 10);
            expect(project.cameraCuts.front().end() == 4);
            const auto after = f.prepare().projectPoint({.3f, .2f, 0}, 4);
            expectWithinAbsoluteError(after.x, before.x, 1.0e-5f);
            expectWithinAbsoluteError(after.y, before.y, 1.0e-5f);
            expect(f.undo.undo());
            expectEquals(static_cast<int>(f.document.project().cameras.size()), 2);
            expectEquals(static_cast<int>(f.document.project().cameraCuts.size()), 1);
        }
        beginTest("The first camera becomes the default view; deleting one removes its cuts");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            motion::Project project;
            project.duration = 10;
            document.reset(std::move(project));
            motion::Id first = 0, second = 0;
            expect(document.addCamera(3, first).wasOk());
            expect(document.project().cameraCuts.empty(), "the first camera needs no cut");
            expect(document.addCamera(5, second).wasOk());
            expectEquals(static_cast<int>(document.project().cameraCuts.size()), 1);
            expect(document.removeCamera(second).wasOk());
            expect(document.project().cameraCuts.empty() && document.project().cameras.size() == 1);
            expect(document.removeCamera(second).failed());
            expect(document.addCamera(11, second).failed(), "cameras are added inside the project");
        }
        beginTest("Targets and parents save, reload, and are cleared with their objects");
        {
            Fixture f; f.initialise();
            expect(f.document.setCameraRig(f.first, f.clip, f.group).wasOk());
            motion::Project loaded;
            const auto result = motion::Document::prepareLoad(f.document.save(), loaded);
            expect(result.wasOk(), result.getErrorMessage());
            expect(loaded.cameras[0].target == f.clip && loaded.cameras[0].parent == f.group);
            expect(f.document.removeClips({f.clip}).wasOk());
            expect(f.document.project().cameras[0].target == 0 && f.document.project().cameras[0].parent == f.group);
            f.document.edit("Remove group", [&](motion::Project& project) { project.groups.clear(); });
            expect(f.document.project().cameras[0].parent == 0);
            auto broken = f.document.save();
            broken.getChildByName("camera")->setAttribute("target", "987654");
            expect(motion::Document::prepareLoad(broken, loaded).failed());
        }
    }
};

static MotionCameraRigTest motionCameraRigTest;
