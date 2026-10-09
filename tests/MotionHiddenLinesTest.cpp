#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/HiddenLines.h"
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/model/MeshFaces.h"

// A camera that hides hidden lines removes the lines behind a mesh's faces,
// baked frame by frame, and the beam draws what remains.
class MotionHiddenLinesTest : public juce::UnitTest {
public:
    MotionHiddenLinesTest() : juce::UnitTest("Motion hidden lines", "Motion") {}

    void runTest() override {
        beginTest("A camera hiding hidden lines removes a cube's far edges");
        auto project = cubeProject(true);
        const motion::PreparedComposition composition(project);
        expect(composition.hiddenLines != nullptr, "the composition bakes hidden lines");
        if (composition.hiddenLines == nullptr) { return; }
        const auto* whole = composition.clips.front().source->drawingAt(0);
        expect(whole != nullptr && whole->faces().triangles.size() == 12, "the cube keeps its six faces as twelve triangles");
        motion::hidden::Solver solver;
        const auto frame = motion::hidden::cull(composition, 1, solver);
        const auto* culled = frame->drawing(0);
        expect(culled != nullptr, "some of the cube's lines are hidden");
        if (culled == nullptr || whole == nullptr) { return; }
        // Seen from a corner, nine of a cube's twelve edges show.
        const auto shown = culled->length() / whole->length();
        expect(shown > .55 && shown < .9, "about three quarters of its length shows: " + juce::String(shown));

        beginTest("A mesh known only by its lines gets the faces they enclose");
        {
            std::vector<std::unique_ptr<osci::Shape>> lines;
            const osci::Point corners[8] {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
            const int edges[12][2] {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : edges) { lines.push_back(std::make_unique<osci::Line>(corners[edge[0]], corners[edge[1]])); }
            expectEquals(static_cast<int>(motion::mesh::facesOfLines(lines).triangles.size()), 12, "a wire cube's six squares, as twelve triangles");
            lines.push_back(std::make_unique<osci::Line>(corners[0], corners[2]));
            expectEquals(static_cast<int>(motion::mesh::facesOfLines(lines).triangles.size()), 12, "a square split by its diagonal is its two triangles");
        }

        beginTest("Without it, nothing is baked and the cube draws whole");
        const motion::PreparedComposition plain(cubeProject(false));
        expect(plain.hiddenLines == nullptr);

        beginTest("Once baked, the beam draws the cube with its hidden lines removed");
        std::atomic<bool> cancel {false};
        motion::hidden::Baker::bakeAll(*composition.hiddenLines, 48000, cancel);
        expect(composition.hiddenLines->complete(), "every frame is baked");
        const auto* baked = composition.hiddenLines->at(1);
        expect(baked != nullptr && baked->drawing(0) != nullptr);
        motion::BeamRenderer beam;
        beam.planCycle(composition, 1, 48000);
        bool drewCulled = false;
        for (const auto& segment : beam.plannedSegments()) {
            if (segment.kind == motion::BeamRenderer::Kind::draw) { drewCulled = segment.drawing == baked->drawing(0); }
        }
        expect(drewCulled, "the planned drawing is the baked one");

        beginTest("Profile: a frame of real meshes, and baking them");
        for (const auto* model : {"teapot.obj", "lamp.obj"}) {
            const auto file = juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("Resources/models").getChildFile(model);
            if (!file.existsAsFile()) { continue; }
            auto meshProject = cubeProject(true);
            auto mesh = std::make_shared<motion::Asset>();
            mesh->id = 2;
            mesh->name = model;
            mesh->extension = ".obj";
            file.loadFileAsData(mesh->data);
            expect(motion::decodeAsset(*mesh).wasOk());
            meshProject.assets = {mesh};
            meshProject.tracks.change(0).clips[0].asset = mesh->id;
            const motion::PreparedComposition prepared(meshProject);
            const auto faces = prepared.clips.front().source->drawingAt(0)->faces().triangles.size();
            motion::hidden::Solver meshSolver;
            const auto started = juce::Time::getMillisecondCounterHiRes();
            double shown = 0;
            for (int index = 0; index < 20; ++index) {
                const auto culledFrame = motion::hidden::cull(prepared, index * .05, meshSolver);
                const auto* drawing = culledFrame->drawing(0);
                shown = drawing != nullptr ? drawing->length() / prepared.clips.front().source->drawingAt(0)->length() : 1.0;
            }
            const auto perFrame = (juce::Time::getMillisecondCounterHiRes() - started) / 20;
            std::atomic<bool> stop {false};
            const auto bakeStarted = juce::Time::getMillisecondCounterHiRes();
            motion::hidden::Baker::bakeAll(*prepared.hiddenLines, 48000, stop);
            const auto bake = juce::Time::getMillisecondCounterHiRes() - bakeStarted;
            logMessage(juce::String(model) + ": " + juce::String(static_cast<int>(faces)) + " triangles, " + juce::String(perFrame, 2) + " ms a frame, "
                + juce::String(static_cast<int>(prepared.hiddenLines->frameCount())) + " frames baked in " + juce::String(bake, 0) + " ms, "
                + juce::String(juce::roundToInt(shown * 100)) + "% of its lines shown");
            expect(shown < .9, "much of a mesh is hidden from any side");
        }
    }

private:
    static motion::Project cubeProject(bool hide) {
        motion::Project project;
        project.duration = 2;
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "Cube.obj";
        asset->extension = ".obj";
        const juce::String cube = "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
            "f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n";
        asset->data.append(cube.toRawUTF8(), cube.getNumBytesAsUTF8());
        motion::decodeAsset(*asset);
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(20, *asset, 0);
        clip.duration = 2;
        clip.properties["rotation.x"] = motion::Curve(30);
        clip.properties["rotation.y"] = motion::Curve(40);
        motion::Track track;
        track.id = 10;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
        motion::Camera camera;
        camera.id = 30;
        camera.hideHiddenLines = hide;
        project.cameras.push_back(camera);
        return project;
    }
};

static MotionHiddenLinesTest motionHiddenLinesTest;
