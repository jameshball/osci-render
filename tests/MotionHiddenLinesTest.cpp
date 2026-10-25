#include <JuceHeader.h>
#include "RepositoryRoot.h"
#include "../Source/motion/export/SignalExporter.h"
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/HiddenLineBaker.h"
#include "../Source/motion/render/HiddenLines.h"
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/model/MeshFaces.h"

// A solid mesh hides the lines behind its faces, baked frame by frame, and
// the beam draws what remains.
class MotionHiddenLinesTest : public juce::UnitTest {
public:
    MotionHiddenLinesTest() : juce::UnitTest("Motion hidden lines", "Motion") {}

    void runTest() override {
        beginTest("A solid cube hides its far edges");
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

        beginTest("Part solid keeps the far edges, dimmed, and the amount can be keyed");
        {
            auto halfway = cubeProject(true);
            halfway.tracks.change(0).clips.front().properties["solid"] = motion::Curve(50.0);
            const motion::PreparedComposition faded(halfway);
            motion::hidden::Workspace fadeSolver;
            const auto fadedFrame = motion::hidden::cull(faded, 1, fadeSolver);
            const auto* kept = fadedFrame != nullptr ? fadedFrame->drawing(0) : nullptr;
            expect(kept != nullptr && whole != nullptr && kept->length() > whole->length() * .95, "every edge is still drawn");
            auto keyed = cubeProject(true);
            auto& solid = keyed.tracks.change(0).clips.front().properties["solid"];
            solid = motion::Curve(0.0);
            solid.setKey({0, 0, motion::Interpolation::linear});
            solid.setKey({2, 100, motion::Interpolation::linear});
            const motion::PreparedComposition rising(keyed);
            expect(rising.hiddenLines != nullptr, "a mesh keyed to become solid bakes its hidden lines");
            expectWithinAbsoluteError(rising.clips.front().solid(1), .5, 1.0e-9);
        }

        beginTest("Only what is solid hides: beside a solid cube, a wireframe one is seen through whole");
        {
            auto pair = cubeProject(true);
            auto& solidCube = pair.tracks.change(0).clips.front();
            solidCube.properties["position.x"] = motion::Curve(-.5);
            solidCube.properties["scale.x"] = solidCube.properties["scale.y"] = solidCube.properties["scale.z"] = motion::Curve(.35);
            auto wireCube = solidCube;
            wireCube.id = 21;
            wireCube.properties["position.x"] = motion::Curve(.5);
            wireCube.properties["solid"] = motion::Curve(0.0);
            motion::Track wireTrack;
            wireTrack.id = 11;
            wireTrack.insert(wireCube, pair.tempo());
            pair.tracks.push_back(wireTrack);
            const motion::PreparedComposition prepared(pair);
            motion::hidden::Workspace pairSolver;
            const auto pairFrame = motion::hidden::cull(prepared, 1, pairSolver);
            const auto* solidDrawn = pairFrame != nullptr ? pairFrame->drawing(0) : nullptr;
            const auto* solidWhole = prepared.clips[0].source->drawingAt(0);
            expect(solidDrawn != nullptr && solidDrawn->length() < solidWhole->length() * .9, "the solid cube hides its own far edges");
            expect(pairFrame != nullptr && pairFrame->drawing(1) == nullptr, "the wireframe cube is drawn whole");
        }

        beginTest("With no camera, what is solid still hides, seen as the Scope's fixed framing sees it");
        {
            auto unframed = cubeProject(true);
            unframed.cameras.clear();
            const motion::PreparedComposition prepared(unframed);
            expect(prepared.hiddenLines != nullptr, "it bakes its hidden lines");
            motion::hidden::Workspace unframedSolver;
            const auto unframedFrame = motion::hidden::cull(prepared, 1, unframedSolver);
            const auto* drawn = unframedFrame != nullptr ? unframedFrame->drawing(0) : nullptr;
            const auto* all = prepared.clips[0].source->drawingAt(0);
            const auto shown = drawn != nullptr ? drawn->length() / all->length() : 1.0;
            expect(shown > .55 && shown < .9, "about three quarters of its length shows: " + juce::String(shown));
        }

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
            // In front of the cube, and all of it on the Scope.
            squareClip.properties["position.z"] = motion::Curve(1);
            squareClip.properties["scale.x"] = motion::Curve(.9);
            squareClip.properties["scale.y"] = motion::Curve(.9);
            squareClip.properties["solid"] = motion::Curve(100.0);
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
                auto slight = sunk.tracks[0].clips[0];
                slight.id = 21;
                slight.properties["scale.x"] = motion::Curve(.4);
                slight.properties["scale.y"] = motion::Curve(.4);
                slight.properties["scale.z"] = motion::Curve(.4);
                slight.properties[axis] = motion::Curve(offset);
                motion::Track track;
                track.id = 11;
                track.insert(slight, sunk.tempo());
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

        beginTest("A floor reaching back past the eye hides the foot of what is sunk in it, and meets it in a line");
        {
            // A cube (about a unit across, as imports are) standing a little
            // way down through a floor that runs from far behind the camera
            // to far in front of it.
            auto standing = cubeProject(true);
            auto cubeClip = standing.tracks[0].clips[0];
            cubeClip.properties["rotation.x"] = motion::Curve(0);
            motion::Track cubeTrack;
            cubeTrack.id = 10;
            cubeTrack.insert(cubeClip, standing.tempo());
            standing.tracks.clear();
            standing.tracks.push_back(cubeTrack);
            auto floor = std::make_shared<motion::Asset>();
            floor->id = 2;
            floor->name = "Floor.obj";
            floor->extension = ".obj";
            const juce::String quad = "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nf 1 2 3 4\n";
            floor->data.append(quad.toRawUTF8(), quad.getNumBytesAsUTF8());
            motion::decodeAsset(*floor);
            standing.assets.push_back(floor);
            auto floorClip = motion::Document::makeClip(21, *floor, 0);
            floorClip.duration = 2;
            floorClip.properties["solid"] = motion::Curve(100.0);
            floorClip.properties["scale.x"] = motion::Curve(100);
            floorClip.properties["scale.z"] = motion::Curve(100);
            floorClip.properties["position.y"] = motion::Curve(-.4);
            motion::Track floorTrack;
            floorTrack.id = 11;
            floorTrack.insert(floorClip, standing.tempo());
            standing.tracks.push_back(floorTrack);
            const motion::PreparedComposition prepared(standing);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto* cube = frame->drawing(0);
            expect(cube != nullptr, "the floor hides part of the cube");
            // Heights in the scene, where the floor is.
            const auto height = [&](osci::Shape& shape, float t) { return prepared.clips[0].processPoint(shape.nextVector(t), 1).y; };
            int below = 0, crossings = 0;
            for (std::size_t index = 0; cube != nullptr && index < cube->shapeCount(); ++index) {
                auto* shape = cube->shape(index);
                crossings += std::abs(height(*shape, 0) + .4f) < 1.0e-3f && std::abs(height(*shape, 1) + .4f) < 1.0e-3f ? 1 : 0;
                for (int step = 0; step <= 20; ++step) { below += height(*shape, step / 20.0f) < -.45f ? 1 : 0; }
            }
            expectEquals(below, 0, "nothing of the cube below the floor shows");
            expect(crossings > 0, "where the cube comes up through the floor is drawn");
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

        // An eye at the origin looking down +Z, as the dicing tests see faces.
        const auto lookingAlongZ = [] {
            motion::hidden::Eye eye;
            eye.right = {1, 0, 0};
            eye.up = {0, 1, 0};
            eye.forward = {0, 0, 1};
            return motion::hidden::DiceView {eye, motion::hidden::Projection {1}, {}, motion::hidden::DepthBuffer::pixel};
        };
        const auto inScene = [](const osci::Point& point) { return std::optional(motion::Vec3 {point.x, point.y, point.z}); };

        beginTest("A bent surface passing behind the eye still hides what it covers at the edge of the view");
        {
            // A wall beside the eye, x = 0.25, running from behind it to far in
            // front: seen nearly edge on, the part just in front of the eye
            // fills the frame's right edge. Diced as a bent surface is, then
            // drawn piece by piece, cut where each comes into view.
            const std::array views {lookingAlongZ()};
            const auto corner = [&](float x, float y, float z) { const osci::Point point(x, y, z); return motion::hidden::DiceCorner {point, *inScene(point)}; };
            motion::hidden::DepthBuffer depth;
            motion::hidden::Dicer dicer;
            dicer.begin(views);
            const auto draw = [&](const motion::hidden::DiceCorner& a, const motion::hidden::DiceCorner& b, const motion::hidden::DiceCorner& c) {
                motion::hidden::drawFlat(views[0].projection, {}, depth, {views[0].eye.see(a.at), views[0].eye.see(b.at), views[0].eye.see(c.at)}, {a.source, b.source, c.source}, true, [](const auto&, const auto&) {});
            };
            dicer.dice(corner(.25f, -2, -3), corner(.25f, -2, 12), corner(.25f, 2, 12), inScene, draw);
            dicer.dice(corner(.25f, -2, -3), corner(.25f, 2, 12), corner(.25f, 2, -3), inScene, draw);
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

        beginTest("A bend cuts a face as finely as it curves it, and a step in it ends the cutting");
        {
            // A face filling much of the frame, a little in front of the eye.
            const std::array views {lookingAlongZ()};
            const auto dice = [&](auto&& bend) {
                const auto corner = [&](float x, float y) { const osci::Point point(x, y, 2); return motion::hidden::DiceCorner {point, *bend(point)}; };
                motion::hidden::Dicer dicer;
                dicer.begin(views);
                std::size_t pieces = 0;
                dicer.dice(corner(-1, -1), corner(1, -1), corner(-1, 1), bend, [&](const auto&, const auto&, const auto&) { ++pieces; });
                return pieces;
            };
            // Unbent it stays flat: halved only until small enough to grid,
            // not into cells a few pixels across (thousands).
            const auto flat = dice(inScene);
            expect(flat > 0 && flat < 1024, "a flat face is cut into few pieces (" + juce::String(static_cast<int>(flat)) + ")");
            // Bulged, it curves, and is cut finer.
            const auto bulge = [&](const osci::Point& point) { return inScene(osci::Point(point.x, point.y, point.z - .3f * (1 - point.x * point.x) * (1 - point.y * point.y))); };
            const auto bulged = dice(bulge);
            expect(bulged > flat, "a curved face is cut finer than a flat one (" + juce::String(static_cast<int>(bulged)) + ")");
            // Crushed into steps, as Bit crush does: an edge across a step stays
            // as long however often it is halved, so halving it stops there.
            const auto crushed = dice([&](const osci::Point& point) { return inScene(osci::Point(std::round(point.x * 8) / 8, std::round(point.y * 8) / 8, point.z)); });
            expect(crushed > 0 && crushed < 100000, "a stepped face is cut into a bounded number of pieces (" + juce::String(static_cast<int>(crushed)) + ")");
            // A bake that stops partway through a face stops its dicing too.
            const auto corner = [&](float x, float y) { const osci::Point point(x, y, 2); return motion::hidden::DiceCorner {point, *bulge(point)}; };
            motion::hidden::Dicer dicer;
            dicer.begin(views);
            std::size_t pieces = 0;
            const auto finished = dicer.dice(corner(-1, -1), corner(1, -1), corner(-1, 1), bulge, [&](const auto&, const auto&, const auto&) { ++pieces; }, [] { return true; });
            expect(!finished && pieces < 1024, "a cancelled face stops being diced (" + juce::String(static_cast<int>(pieces)) + " pieces)");
        }

        beginTest("A bend that folds a face across the view again and again is cut within a budget");
        {
            // A fine ripple, as a camera close to a rippled surface sees it:
            // every fold throws the face across the whole view, so halving
            // alone would go on to its deepest (minutes, and gigabytes).
            const std::array views {lookingAlongZ()};
            const auto folded = [&](const osci::Point& point) { return inScene(osci::Point(point.x + 2 * std::sin(500 * point.y), point.y, point.z)); };
            const auto corner = [&](float x, float y) { const osci::Point point(x, y, 1); return motion::hidden::DiceCorner {point, *folded(point)}; };
            motion::hidden::Dicer dicer;
            dicer.begin(views);
            std::size_t pieces = 0;
            const auto finished = dicer.dice(corner(-1, -1), corner(1, -1), corner(-1, 1), folded, [&](const auto&, const auto&, const auto&) { ++pieces; }, [] { return false; });
            expect(finished && pieces > 0 && pieces <= motion::hidden::Dicer::budget + 1, "a folded face is cut into a bounded number of pieces (" + juce::String(static_cast<juce::int64>(pieces)) + ")");
        }

        beginTest("A face is diced once as finely as the finest view that sees it needs");
        {
            // The same bulged face, seen from twice as far, needs half the
            // detail; seen by both, it is cut as the nearer needs; seen by
            // neither (both looking away), it is dropped.
            const auto near = lookingAlongZ();
            auto far = near;
            far.eye.position = {0, 0, -2};
            auto away = near;
            away.eye.forward = {0, 0, -1};
            away.eye.right = {-1, 0, 0};
            const auto bulge = [&](const osci::Point& point) { return inScene(osci::Point(point.x, point.y, point.z - .3f * (1 - point.x * point.x) * (1 - point.y * point.y))); };
            const auto dice = [&](std::span<const motion::hidden::DiceView> views) {
                const auto corner = [&](float x, float y) { const osci::Point point(x, y, 2); return motion::hidden::DiceCorner {point, *bulge(point)}; };
                motion::hidden::Dicer dicer;
                dicer.begin(views);
                std::size_t pieces = 0;
                dicer.dice(corner(-1, -1), corner(1, -1), corner(-1, 1), bulge, [&](const auto&, const auto&, const auto&) { ++pieces; });
                return pieces;
            };
            const std::array nearOnly {near}, farOnly {far}, neither {away};
            const std::array both {far, near};
            const auto nearPieces = dice(nearOnly), farPieces = dice(farOnly), bothPieces = dice(both);
            expect(farPieces < nearPieces, "a farther view needs fewer pieces (" + juce::String(static_cast<int>(farPieces)) + " against " + juce::String(static_cast<int>(nearPieces)) + ")");
            expectEquals(static_cast<int>(bothPieces), static_cast<int>(nearPieces), "seen by both, it is cut as the nearer needs");
            expectEquals(static_cast<int>(dice(neither)), 0, "a face no view sees is dropped");
        }

        beginTest("Diced once for several views, a face is fine enough in every view that sees it");
        {
            // No piece over twice the grid's reach across in any view that sees
            // it (a length lands longer toward a view's edge than at its
            // middle), and hardly a point further from the bent surface than a
            // pixel on screen or the room the depth test leaves a surface (a
            // share of its inverse depth, and of how much that changes across a
            // pixel): flatness is judged from a few points of each piece.
            using motion::hidden::Dicer;
            const auto view = [&](motion::Vec3 at, double focal, bool parallel = false) {
                auto made = lookingAlongZ();
                made.eye.position = at;
                made.projection.focalLength = focal;
                made.projection.parallel = parallel;
                made.projection.farthest = made.projection.span = 8;
                return made;
            };
            const std::array views {view({0, 0, 0}, 1), view({.6, .4, -.5}, .5), view({0, 0, -3}, 3), view({0, 0, -2}, .4, true)};
            const auto bulge = [&](const osci::Point& point) { return inScene(osci::Point(point.x, point.y, point.z - .3f * (1 - point.x * point.x) * (1 - point.y * point.y))); };
            const auto twist = [&](const osci::Point& point) {
                const auto angle = 2.0 * point.y;
                return std::optional(motion::Vec3 {point.x * std::cos(angle) + (point.z - 2) * std::sin(angle), point.y, 2 - point.x * std::sin(angle) + (point.z - 2) * std::cos(angle)});
            };
            const auto ripple = [&](const osci::Point& point) { return inScene(osci::Point(point.x, point.y, point.z + .1f * std::sin(10.0f * (point.x * point.x + point.y * point.y)))); };
            const std::array<std::array<osci::Point, 3>, 2> faces {{{osci::Point(-1, -1, 2), osci::Point(1, -1, 2), osci::Point(-1, 1, 2)}, {osci::Point(.5f, .5f, 1.2f), osci::Point(1.5f, .5f, 2), osci::Point(.5f, 1.2f, 3)}}};
            int checked = 0, sampled = 0, wide = 0, curved = 0, deep = 0;
            const auto check = [&](auto&& bend) {
                for (const auto& face : faces) {
                    Dicer dicer;
                    dicer.begin(views);
                    std::vector<std::array<motion::hidden::DiceCorner, 3>> pieces;
                    const auto corner = [&](const osci::Point& point) { return motion::hidden::DiceCorner {point, *bend(point)}; };
                    dicer.dice(corner(face[0]), corner(face[1]), corner(face[2]), bend, [&](const auto& a, const auto& b, const auto& c) { pieces.push_back({a, b, c}); });
                    for (const auto& seenBy : views) {
                        const auto& projection = seenBy.projection;
                        const auto onScreen = [&](motion::Vec3 world) { return projection(seenBy.eye.see(world)); };
                        for (const auto& piece : pieces) {
                            // Only what this view sees whole, in front of its eye and in its frame.
                            std::array<motion::hidden::Screen, 3> at {};
                            bool whole = true, inFrame = false;
                            for (std::size_t index = 0; index < 3 && whole; ++index) {
                                const auto point = onScreen(piece[index].at);
                                whole = point.has_value();
                                if (whole) {
                                    at[index] = *point;
                                    inFrame = inFrame || (std::abs(point->x) <= motion::hidden::DepthBuffer::overscan && std::abs(point->y) <= motion::hidden::DepthBuffer::overscan);
                                }
                            }
                            if (!whole || !inFrame) { continue; }
                            ++checked;
                            const auto pixels = [&](const motion::hidden::Screen& u, const motion::hidden::Screen& v) { return std::hypot(v.x - u.x, v.y - u.y) / seenBy.pixel; };
                            const auto longest = std::max({pixels(at[0], at[1]), pixels(at[1], at[2]), pixels(at[2], at[0])});
                            wide += longest > 2 * Dicer::gridReach ? 1 : 0;
                            // The flat piece's inverse depth over the screen, as the depth buffer has it.
                            const auto ux = at[1].x - at[0].x, uy = at[1].y - at[0].y, vx = at[2].x - at[0].x, vy = at[2].y - at[0].y;
                            const auto area = ux * vy - uy * vx;
                            if (!(std::abs(area) > 0)) { continue; }
                            const auto dwdx = ((at[1].w - at[0].w) * vy - (at[2].w - at[0].w) * uy) / area, dwdy = ((at[2].w - at[0].w) * ux - (at[1].w - at[0].w) * vx) / area;
                            const auto room = [&](double w) { return motion::hidden::DepthBuffer::depthPush * std::abs(w) + motion::hidden::DepthBuffer::insidePush * (std::abs(dwdx) + std::abs(dwdy)) * seenBy.pixel; };
                            for (const auto& [s, t] : std::array<std::pair<double, double>, 5> {{{.25, .25}, {.6, .2}, {.2, .6}, {.4, .4}, {.1, .1}}}) {
                                const auto mixed = [&](auto u, auto v, auto w) { return u + (v - u) * s + (w - u) * t; };
                                const auto source = osci::Point(static_cast<float>(mixed(piece[0].source.x, piece[1].source.x, piece[2].source.x)), static_cast<float>(mixed(piece[0].source.y, piece[1].source.y, piece[2].source.y)), static_cast<float>(mixed(piece[0].source.z, piece[1].source.z, piece[2].source.z)));
                                const auto bent = bend(source);
                                const auto flat = mixed(piece[0].at, piece[1].at, piece[2].at);
                                const auto trueAt = onScreen(*bent), flatAt = onScreen(flat);
                                if (!trueAt.has_value() || !flatAt.has_value()) { continue; }
                                ++sampled;
                                curved += pixels(*trueAt, *flatAt) > 1 ? 1 : 0;
                                const auto plane = at[0].w + dwdx * (trueAt->x - at[0].x) + dwdy * (trueAt->y - at[0].y);
                                deep += std::abs(trueAt->w - plane) > room(trueAt->w) ? 1 : 0;
                            }
                        }
                    }
                }
            };
            check(bulge);
            check(twist);
            check(ripple);
            expect(checked > 1000, "pieces are checked in every view (" + juce::String(checked) + ")");
            expectEquals(wide, 0, "no piece is over twice the grid's reach in any view that sees it");
            expect(curved * 1000 < sampled, "hardly a point strays a pixel from the bent surface on screen (" + juce::String(curved) + " of " + juce::String(sampled) + ")");
            expect(deep * 1000 < sampled, "hardly a point strays from the bent surface past the depth test's room (" + juce::String(deep) + " of " + juce::String(sampled) + ")");
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

        beginTest("A point source's fingerprint knows which frames it draws, not just which frame");
        {
            // A ring beside the cube, then the same project with its source replaced.
            const auto ringAsset = [this](float radius) {
                std::vector<motion::PointSample> ring;
                for (int index = 0; index < 64; ++index) {
                    const auto angle = index * 2 * juce::MathConstants<double>::pi / 64;
                    ring.push_back({static_cast<float>(std::cos(angle)) * radius, 0.0f, static_cast<float>(std::sin(angle)) * radius, 1.0f, 0.0f, 0.0f});
                }
                auto frames = motion::PreparedPointFrames::create(30, 1, ring.size(), std::move(ring));
                expect(frames.source != nullptr, juce::String(frames.error));
                auto points = std::make_shared<motion::Asset>();
                points->id = 3;
                points->name = "Ring.lua";
                points->extension = ".lua";
                points->source = std::make_shared<motion::PreparedSource>(frames.source);
                return points;
            };
            auto project = cubeProject(true);
            project.assets.push_back(ringAsset(.9f));
            auto clip = motion::Document::makeClip(21, *project.assets.back(), 0);
            clip.duration = 2;
            motion::Track track;
            track.id = 11;
            track.insert(clip, project.tempo());
            project.tracks.push_back(track);
            auto edited = project;
            edited.assets.back() = ringAsset(.5f);
            const motion::PreparedComposition before(project), again(project), replaced(edited);
            motion::hidden::Fingerprint first, second, third;
            first.take(before, 1);
            second.take(again, 1);
            third.take(replaced, 1);
            expect(first == second, "the same frames prepared again depend on the same");
            expect(!(first == third), "a replaced source does not");
        }

        beginTest("A bake takes over hatching only from a bake of the same beam frequency");
        {
            motion::hidden::Bake before(nullptr, 10, 1, {}), same(nullptr, 10, 1, {}), faster(nullptr, 10, 1, {});
            for (auto* bake : {&before, &same, &faster}) { bake->settings.hatches = true; }
            faster.settings.beamRate = before.settings.beamRate * 4;
            before.publish(0, std::make_shared<const motion::hidden::Frame>(), 7);
            same.inherit(before);
            faster.inherit(before);
            expect(same.inherited(7) != nullptr, "the same beam frequency takes the frame over");
            expect(faster.inherited(7) == nullptr, "another bakes its hatching afresh");
        }

        beginTest("A bake claims frames from its focus on, follows it, and finds a frame given back");
        {
            motion::hidden::Bake bake(nullptr, 10, 1, {});
            expectEquals(static_cast<int>(bake.frameCount()), 11);
            bake.focusOn(8);
            std::vector<int> order;
            for (int index = 0; index < 4; ++index) { order.push_back(static_cast<int>(*bake.claim())); }
            expect(order == std::vector<int> {8, 9, 10, 0}, "from the focus to the end, then the start");
            expect(bake.state(9) == motion::hidden::Bake::State::baking);
            bake.focusOn(3);
            expectEquals(static_cast<int>(*bake.claim()), 3);
            bake.release(3);
            bake.focusOn(2);
            expectEquals(static_cast<int>(*bake.claim()), 2);
            expectEquals(static_cast<int>(*bake.claim()), 3, "a frame given back is claimed again");
            for (int frame = 0; frame < 11; ++frame) { bake.skip(static_cast<std::size_t>(frame)); }
            expect(bake.complete() && !bake.claim().has_value());
        }

        beginTest("A bake after an edit takes over the frames the edit left as they were");
        {
            // A cube turning through its first second; the edit is a key after it.
            auto project = cubeProject(true);
            auto& turn = project.tracks.change(0).clips[0].properties["rotation.y"];
            turn.setKey({0, 0, motion::Interpolation::linear});
            turn.setKey({1, 90, motion::Interpolation::linear});
            const motion::PreparedComposition before(project);
            std::atomic<bool> stop {false};
            motion::hidden::Baker::bakeAll(*before.hiddenLines, 48000, stop);
            auto edited = project;
            edited.tracks.change(0).clips[0].properties["rotation.y"].setKey({1.5, 120, motion::Interpolation::linear});
            const auto sharedUntil = [&](const motion::PreparedComposition& after) {
                after.hiddenLines->inherit(*before.hiddenLines);
                motion::hidden::Baker::bakeAll(*after.hiddenLines, 48000, stop);
                int kept = 0, changed = 0;
                for (std::size_t frame = 0; frame < after.hiddenLines->frameCount(); ++frame) {
                    const auto time = after.hiddenLines->timeOf(frame);
                    const auto same = after.hiddenLines->at(time) == before.hiddenLines->at(time);
                    if (time <= 1) { kept += same ? 1 : 0; }
                    if (time > 1.05 && time < 1.45) { changed += same ? 0 : 1; }
                }
                return std::pair(kept, changed);
            };
            const auto frames = static_cast<int>(before.hiddenLines->frameOf(1)) + 1;
            const auto [kept, changed] = sharedUntil(motion::PreparedComposition(edited));
            expectEquals(kept, frames, "every frame up to the edit is the bake before's");
            expect(changed > 0, "the frames it changed are baked again");
            const auto [keptElsewhere, _] = sharedUntil(motion::PreparedComposition(edited, 48000, nullptr, motion::CompositionPurpose::signal, motion::hidden::Reach {2, 2}));
            expectEquals(keptElsewhere, 0, "nothing is taken over from a bake of what a different Scope shows");
        }

        beginTest("An export taking over the editor's bake writes just what one baking afresh does");
        {
            auto project = cubeProject(true);
            auto& turn = project.tracks.change(0).clips[0].properties["rotation.y"];
            turn.setKey({0, 0, motion::Interpolation::linear});
            turn.setKey({1, 90, motion::Interpolation::linear});
            const motion::PreparedComposition editor(project);
            std::atomic<bool> stop {false};
            motion::hidden::Baker::bakeAll(*editor.hiddenLines, 48000, stop);
            const juce::TemporaryFile fresh(".wav"), reused(".wav");
            expect(motion::SignalExporter::write(project, fresh.getFile(), 48000, stop).wasOk());
            expect(motion::SignalExporter::write(project, reused.getFile(), 48000, stop, nullptr, motion::squareScope(), editor.hiddenLines.get()).wasOk());
            juce::MemoryBlock a, b;
            expect(fresh.getFile().loadFileAsData(a) && reused.getFile().loadFileAsData(b));
            expect(a.getSize() > 0 && a == b, "the signal is the same sample for sample");
        }

        beginTest("On an XY-only scope a line cut in two is not joined again across its gap");
        {
            using motion::hidden::Screen;
            // Two lines, each cut in two around x = 0, then one more stroke.
            const auto stroke = [](double ax, double ay, double bx, double by) { return motion::hidden::TourStroke {Screen {ax, ay, 1}, Screen {bx, by, 1}}; };
            const std::vector<motion::hidden::TourStroke> strokes {stroke(-1, 0, -.2, 0), stroke(.2, 0, 1, 0), stroke(-1, .1, -.2, .1), stroke(.2, .1, 1, .1), stroke(1, .5, 1, .6)};
            const auto order = motion::hidden::tour(strokes);
            expectEquals(static_cast<int>(order.size()), 5);
            expect(order.front().stroke == 0 && !order.front().reversed && order.back().stroke == 4 && !order.back().reversed, "the first and last strokes stay where they were");
            for (std::size_t index = 1; index < order.size(); ++index) {
                const auto& from = order[index - 1].reversed ? strokes[order[index - 1].stroke].start : strokes[order[index - 1].stroke].end;
                const auto& to = order[index].reversed ? strokes[order[index].stroke].end : strokes[order[index].stroke].start;
                expect(!(std::abs(from->x) == .2 && std::abs(to->x) == .2 && from->y == to->y), "no jump joins a line up again across its gap");
            }
            std::vector<int> seen(strokes.size(), 0);
            for (const auto& step : order) { ++seen[step.stroke]; }
            expect(std::all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }), "every stroke drawn once");

            auto lit = cubeProject(true);
            auto xyOnly = cubeProject(true);
            xyOnly.scope.xyOnly = true;
            const motion::PreparedComposition withBrightness(lit), without(xyOnly);
            motion::hidden::Workspace a, b;
            const auto litFrame = motion::hidden::cull(withBrightness, 1, a), xyFrame = motion::hidden::cull(without, 1, b);
            const auto* drawn = litFrame->drawing(0);
            const auto* ordered = xyFrame->drawing(0);
            expect(drawn != nullptr && ordered != nullptr);
            if (drawn != nullptr && ordered != nullptr) {
                expectWithinAbsoluteError(ordered->length(), drawn->length(), 1.0e-6 * drawn->length());
                const auto same = [](osci::Point p, osci::Point q) { return std::abs(p.x - q.x) < 1.0e-6f && std::abs(p.y - q.y) < 1.0e-6f && std::abs(p.z - q.z) < 1.0e-6f; };
                expect(same(ordered->sample(0), drawn->sample(0)) && same(ordered->sample(1), drawn->sample(1)), "the beam enters and leaves the cube where it did");
                expect(ordered->minimumTraversalSamples(false) <= drawn->minimumTraversalSamples(false), "no more strokes than before");
            }
        }

        beginTest("Profile: a frame of real meshes, and baking them");
        for (const auto& [model, bulge] : {std::pair {"teapot.obj", false}, std::pair {"lamp.obj", false}, std::pair {"teapot.obj", true}}) {
            const auto file = repositoryRoot().getChildFile("Resources/models").getChildFile(model);
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
        clip.properties["solid"] = motion::Curve(hide ? 100.0 : 0.0);
        motion::Track track;
        track.id = 10;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
        motion::Camera camera;
        camera.id = 30;
        project.cameras.push_back(camera);
        return project;
    }
};

static MotionHiddenLinesTest motionHiddenLinesTest;
