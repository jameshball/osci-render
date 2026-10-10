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
        motion::hidden::Workspace solver;
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

        beginTest("A bent mesh hides exactly what its bent faces hide");
        {
            auto bent = cubeProject(true);
            auto bulge = motion::makeEffect(50, *motion::effectDefinition("bulge"));
            bulge.properties["bulge"] = motion::Curve(.9);
            bent.tracks.change(0).effects.push_back(bulge);
            const motion::PreparedComposition bulged(bent);
            motion::hidden::Workspace bentSolver;
            const auto bentFrame = motion::hidden::cull(bulged, 1, bentSolver);
            const auto* source = bulged.clips.front().source->drawingAt(0);
            const auto* drawn = bentFrame->drawing(0);
            const auto truth = rayCastShown(bulged, 0, 1);
            const auto shown = drawn != nullptr ? drawn->length() / source->length() : 1.0;
            logMessage("Bulged cube: " + juce::String(shown, 4) + " of its length shown; ray casting finds " + juce::String(truth, 4));
            expectWithinAbsoluteError(shown, truth, .02, "the bulged cube's visible length matches ray casting its bulged surface");
        }

        beginTest("Point sources are hidden behind meshes, keeping their colours");
        {
            auto withPoints = cubeProject(true);
            // A red ring around the cube, half of it behind.
            std::vector<motion::PointSample> ring;
            for (int index = 0; index < 256; ++index) {
                const auto angle = index * 2 * juce::MathConstants<double>::pi / 256;
                ring.push_back({static_cast<float>(std::cos(angle) * .9), 0.0f, static_cast<float>(std::sin(angle) * .9), 1.0f, 0.0f, 0.0f});
            }
            auto frames = motion::PreparedPointFrames::create(30, 1, ring.size(), std::move(ring));
            expect(frames.source != nullptr, juce::String(frames.error));
            auto points = std::make_shared<motion::Asset>();
            points->id = 3;
            points->name = "Ring.lua";
            points->extension = ".lua";
            points->source = std::make_shared<motion::PreparedSource>(frames.source);
            withPoints.assets.push_back(points);
            auto ringClip = motion::Document::makeClip(21, *points, 0);
            ringClip.duration = 2;
            motion::Track ringTrack;
            ringTrack.id = 11;
            ringTrack.insert(ringClip, withPoints.tempo());
            withPoints.tracks.push_back(ringTrack);
            const motion::PreparedComposition prepared(withPoints);
            motion::hidden::Workspace ringSolver;
            const auto ringFrame = motion::hidden::cull(prepared, 1, ringSolver);
            const auto* drawn = ringFrame->drawing(1);
            expect(drawn != nullptr, "the ring loses its part behind the cube");
            if (drawn != nullptr) {
                const auto whole = 2 * juce::MathConstants<double>::pi * .9;
                expect(drawn->length() < whole * .9 && drawn->length() > whole * .5, "most of the ring shows: " + juce::String(drawn->length() / whole));
                expect(drawn->sample(.3).r == 1.0f && drawn->sample(.3).g == 0.0f, "what shows keeps its colour");
            }
        }

        beginTest("A flat drawing's fill hides what is behind it, never its own outline");
        {
            auto behind = cubeProject(true);
            auto square = std::make_shared<motion::Asset>();
            square->id = 4;
            square->name = "Square.svg";
            square->extension = ".svg";
            const juce::String svg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><rect x="10" y="10" width="80" height="80" fill="white"/></svg>)";
            square->data.append(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
            expect(motion::decodeAsset(*square).wasOk());
            expect(!square->source->drawingAt(0)->faces().empty() && square->source->drawingAt(0)->faces().planar, "a filled SVG has a fill that hides");
            behind.assets.push_back(square);
            auto squareClip = motion::Document::makeClip(22, *square, 0);
            squareClip.duration = 2;
            squareClip.properties["position.z"] = motion::Curve(1.5);
            squareClip.properties["scale.x"] = motion::Curve(1.2);
            squareClip.properties["scale.y"] = motion::Curve(1.2);
            motion::Track squareTrack;
            squareTrack.id = 12;
            squareTrack.insert(squareClip, behind.tempo());
            behind.tracks.push_back(squareTrack);
            const motion::PreparedComposition prepared(behind);
            motion::hidden::Workspace fillSolver;
            const auto fillFrame = motion::hidden::cull(prepared, 1, fillSolver);
            const auto* cube = fillFrame->drawing(0);
            const auto* cubeWhole = prepared.clips[0].source->drawingAt(0);
            expect(cube != nullptr && cube->length() < cubeWhole->length() * .5, "the square in front hides most of the cube");
            expect(fillFrame->drawing(1) == nullptr, "the square's own outline is untouched");
        }

        beginTest("Where one mesh is sunk into another, the line they cross along is drawn");
        {
            // A small cube pushed partway into the big one: where it comes out
            // is an edge of what is seen, though neither cube has it.
            float half = 0;
            const auto withSmallCube = [&](const char* axis, float offset) {
                auto sunk = cubeProject(true);
                auto small = sunk.tracks[0].clips[0];
                small.id = 21;
                small.properties["scale.x"] = motion::Curve(.4);
                small.properties["scale.y"] = motion::Curve(.4);
                small.properties["scale.z"] = motion::Curve(.4);
                small.properties[axis] = motion::Curve(offset);
                motion::Track track;
                track.id = 11;
                track.insert(small, sunk.tempo());
                sunk.tracks.push_back(track);
                return sunk;
            };
            // Lines of the big cube's culled drawing that lie on none of its edges.
            const auto newLines = [&](const motion::Project& sunkProject) {
                const motion::PreparedComposition prepared(sunkProject);
                motion::hidden::Workspace buffer;
                const auto frame = motion::hidden::cull(prepared, 1, buffer);
                const auto* whole = prepared.clips.front().source->drawingAt(0);
                const auto* culled = frame->drawing(0);
                int count = 0;
                for (std::size_t index = 0; culled != nullptr && index < culled->shapeCount(); ++index) {
                    auto* shape = culled->shape(index);
                    const auto middle = shape->nextVector(.5f);
                    double nearest = 1.0e9;
                    for (std::size_t edge = 0; edge < whole->shapeCount(); ++edge) {
                        auto* line = whole->shape(edge);
                        const auto a = line->nextVector(0), b = line->nextVector(1);
                        const motion::Vec3 ab {b.x - a.x, b.y - a.y, b.z - a.z}, am {middle.x - a.x, middle.y - a.y, middle.z - a.z};
                        const auto t = std::clamp(am.dot(ab) / std::max(ab.dot(ab), 1.0e-12), 0.0, 1.0);
                        nearest = std::min(nearest, (am - ab * t).length());
                    }
                    if (nearest > 1.0e-3) { ++count; }
                }
                return count;
            };
            // Centred on the big cube's surface facing the camera, so it is
            // half in, half out where it can be seen.
            {
                const motion::PreparedComposition sized(cubeProject(true));
                const auto* whole = sized.clips.front().source->drawingAt(0);
                for (std::size_t index = 0; index < whole->shapeCount(); ++index) { half = std::max(half, std::abs(whole->shape(index)->nextVector(0).x)); }
            }
            expect(newLines(withSmallCube("position.z", half)) > 0, "the crossing shows on the big cube");
            expectEquals(newLines(withSmallCube("position.x", 3.0f)), 0, "a cube apart from it adds nothing");
        }

        beginTest("Hidden lines are removed out to the Scope's edges, a little past the frame");
        {
            // The Scope shows a unit of signal across less than half its
            // canvas: a little past -1 to 1. A composition's hidden lines
            // reach as far, and a face there hides what is behind it.
            const motion::PreparedComposition prepared(cubeProject(true));
            const auto shown = 1.0 / VisualiserGeometry::signalGain;
            expect(prepared.hiddenLineReach.x >= shown && prepared.hiddenLineReach.y >= shown, "the bake covers what the Scope shows");
            motion::hidden::DepthBuffer depth(prepared.hiddenLineReach);
            const auto at = [](double x, double y) { return motion::hidden::Screen {x, y, .25}; };
            depth.triangle(at(1.08, -.1), at(1.12, -.1), at(1.12, .1));
            expect(!depth.shows({1.11, -.05, .2}), "a face just inside the Scope's edge hides what is behind it");
        }

        beginTest("A camera among a mesh's faces, some passing behind it, culls them cut at the near plane");
        {
            // The camera a unit in front of the cube's middle: the faces
            // round it pass behind the eye and are cut where they come into view.
            auto close = cubeProject(true);
            close.cameras.front().properties["position.z"] = motion::Curve(1);
            const motion::PreparedComposition prepared(close);
            motion::hidden::Workspace workspace;
            for (const auto time : {0.0, .5, 1.0}) {
                const auto frame = motion::hidden::cull(prepared, time, workspace);
                expect(frame != nullptr, "a frame is culled with the camera among the faces");
            }
        }

        beginTest("A bent surface passing behind the eye still hides what it covers at the edge of the view");
        {
            // A wall beside the eye, x = 0.25, running from behind it to far in
            // front: seen nearly edge on, the part just in front of the eye
            // fills the frame's right edge. Diced as a bent surface is.
            const auto bend = [](const osci::Point& point) -> std::optional<motion::hidden::Screen> {
                if (!(point.z > motion::hidden::nearPlane)) { return std::nullopt; }
                return motion::hidden::Screen {point.x / point.z, point.y / point.z, 1.0 / point.z};
            };
            const auto corner = [&](float x, float y, float z) { const osci::Point point(x, y, z); return motion::hidden::DiceCorner {point, bend(point)}; };
            motion::hidden::DepthBuffer depth;
            motion::hidden::Dicer dicer;
            const auto draw = [&](const motion::hidden::DiceCorner& a, const motion::hidden::DiceCorner& b, const motion::hidden::DiceCorner& c) { depth.triangle(*a.at, *b.at, *c.at); };
            dicer.dice(corner(.25f, -2, -3), corner(.25f, -2, 12), corner(.25f, 2, 12), bend, draw);
            dicer.dice(corner(.25f, -2, -3), corner(.25f, 2, 12), corner(.25f, 2, -3), bend, draw);
            int shown = 0, total = 0;
            for (double x = .8; x < 1.0; x += .01) {
                for (double y = -.5; y <= .5; y += .25) {
                    // Twice as far as the wall along the same sight line.
                    const auto wall = .25 / x;
                    shown += depth.shows({x, y, 1 / (2 * wall)}) ? 1 : 0;
                    ++total;
                }
            }
            expectEquals(shown, 0, "nothing behind the wall shows near the frame's edge (" + juce::String(shown) + " of " + juce::String(total) + ")");
        }

        beginTest("A bake reuses a frame for a time that depends on just the same");
        {
            // A still cube: every frame is the first, shared. Turning it, none is.
            auto still = cubeProject(true);
            const motion::PreparedComposition prepared(still);
            std::atomic<bool> stop {false};
            motion::hidden::Baker::bakeAll(*prepared.hiddenLines, 48000, stop);
            std::set<const motion::PreparedDrawing*> drawings;
            for (std::size_t frame = 0; frame < prepared.hiddenLines->frameCount(); ++frame) {
                const auto time = prepared.hiddenLines->timeOf(frame);
                const auto* baked = prepared.hiddenLines->at(time);
                expect(baked != nullptr, "every frame is baked");
                // The cube plays until two seconds.
                if (baked != nullptr && time < 2) {
                    expect(baked->drawing(0) != nullptr, "the cube's hidden lines are removed at " + juce::String(time));
                    drawings.insert(baked->drawing(0));
                }
            }
            expect(drawings.size() <= static_cast<std::size_t>(std::thread::hardware_concurrency()), "a still cube is culled at most once a worker: " + juce::String(static_cast<int>(drawings.size())));
            motion::hidden::Fingerprint first, second;
            first.take(prepared, 0);
            second.take(prepared, 1);
            expect(first == second, "a still cube depends on the same at any time");
            auto turning = cubeProject(true);
            auto& turn = turning.tracks.change(0).clips[0].properties["rotation.y"];
            turn.setKey({0, 0, motion::Interpolation::linear});
            turn.setKey({2, 90, motion::Interpolation::linear});
            const motion::PreparedComposition moving(turning);
            first.take(moving, 0);
            second.take(moving, 1);
            expect(!(first == second), "a turning cube does not");
        }

        beginTest("Profile: a frame of real meshes, and baking them");
        for (const auto& [model, bulge] : {std::pair {"teapot.obj", false}, std::pair {"lamp.obj", false}, std::pair {"teapot.obj", true}}) {
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
            if (bulge) { meshProject.tracks.change(0).effects.push_back(motion::makeEffect(51, *motion::effectDefinition("bulge"))); }
            const motion::PreparedComposition prepared(meshProject);
            const auto faces = prepared.clips.front().source->drawingAt(0)->faces().triangles.size();
            motion::hidden::Workspace meshSolver;
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
            logMessage(juce::String(model) + (bulge ? " bulged" : "") + ": " + juce::String(static_cast<int>(faces)) + " triangles, " + juce::String(perFrame, 2) + " ms a frame, "
                + juce::String(static_cast<int>(prepared.hiddenLines->frameCount())) + " frames baked in " + juce::String(bake, 0) + " ms, "
                + juce::String(juce::roundToInt(shown * 100)) + "% of its lines shown");
            expect(shown < .9, "much of a mesh is hidden from any side");
        }
    }

private:
    // Whether the ray from the eye to `point` meets the triangle before it.
    static bool hiddenBy(motion::Vec3 point, motion::Vec3 a, motion::Vec3 b, motion::Vec3 c) {
        const auto e1 = b - a, e2 = c - a, p = point.cross(e2);
        const auto determinant = e1.dot(p);
        if (std::abs(determinant) < 1.0e-14) { return false; }
        const auto inverse = 1.0 / determinant;
        const auto s = motion::Vec3 {} - a;
        const auto u = s.dot(p) * inverse;
        if (u < 0 || u > 1) { return false; }
        const auto q = s.cross(e1);
        const auto v = point.dot(q) * inverse;
        if (v < 0 || u + v > 1) { return false; }
        const auto t = e2.dot(q) * inverse;
        return t > 0 && t < 1 - 3.0e-3;
    }

    // The fraction of a clip's lines that show, by ray casting many points of
    // them against a fine grid of its own faces, each point bent as drawn.
    static double rayCastShown(const motion::PreparedComposition& composition, std::size_t clipIndex, double time) {
        const auto& clip = composition.clips[clipIndex];
        const auto* drawing = clip.source->drawingAt(0);
        const auto eye = [&](osci::Point point) { return composition.eyePoint(clip.processPoint(point, time), time).value_or(motion::Vec3 {}); };
        std::vector<std::array<motion::Vec3, 3>> surface;
        constexpr int level = 24;
        const auto& mesh = drawing->faces();
        for (const auto& triangle : mesh.triangles) {
            const auto& a = mesh.vertices[triangle[0]];
            const auto& b = mesh.vertices[triangle[1]];
            const auto& c = mesh.vertices[triangle[2]];
            const auto at = [&](int i, int j) {
                const auto u = static_cast<float>(i) / level, v = static_cast<float>(j) / level;
                return eye(osci::Point(a.x + (b.x - a.x) * u + (c.x - a.x) * v, a.y + (b.y - a.y) * u + (c.y - a.y) * v, a.z + (b.z - a.z) * u + (c.z - a.z) * v));
            };
            for (int j = 0; j < level; ++j) {
                for (int i = 0; i + j < level; ++i) {
                    surface.push_back({at(i, j), at(i + 1, j), at(i, j + 1)});
                    if (i + j + 1 < level) { surface.push_back({at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)}); }
                }
            }
        }
        double shown = 0, total = 0;
        for (std::size_t index = 0; index < drawing->shapeCount(); ++index) {
            auto* shape = drawing->shape(index);
            if (shape == nullptr || !(shape->length() > 0)) { continue; }
            constexpr int samples = 60;
            for (int sample = 0; sample < samples; ++sample) {
                const auto point = eye(shape->nextVector((sample + .5f) / samples));
                const auto hidden = std::any_of(surface.begin(), surface.end(), [&](const auto& face) { return hiddenBy(point, face[0], face[1], face[2]); });
                total += shape->length() / samples;
                shown += hidden ? 0 : shape->length() / samples;
            }
        }
        return total > 0 ? shown / total : 1;
    }

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
