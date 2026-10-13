#include <JuceHeader.h>
#include <set>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/render/HiddenLineBaker.h"
#include "../Source/motion/render/HiddenLines.h"

// How a surface draws itself: its edges, its outline as the eye sees it, and
// patterns over it that show its shape, each as bright as asked and lit as
// the faces they lie on are.
class MotionLineStyleTest : public juce::UnitTest {
public:
    MotionLineStyleTest() : juce::UnitTest("Motion line styles", "Motion") {}

    void runTest() override {
        beginTest("An outline alone draws a ball as the camera sees its edge");
        {
            const auto rim = [&](bool hiding) {
                const motion::PreparedComposition prepared(scene({{"edges", 0}, {"outline", 100}}, false, hiding));
                expect(prepared.hiddenLines != nullptr, "a surface drawn by its outline is baked, with no lights and no camera hiding lines");
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, 1, workspace);
                const auto eye = prepared.viewpoint(1)->position;
                // Where the line to the eye grazes the ball, its direction
                // from the middle is this far towards the eye. The ball is
                // the generator's, 16 faces round: its outline is round.
                const auto toward = radius / eye.length();
                double length = 0;
                int seen = 0, onRim = 0;
                const auto* drawn = frame->drawing(0);
                for (std::size_t index = 0; drawn != nullptr && index < drawn->shapeCount(); ++index) {
                    length += drawn->shape(index)->length();
                }
                for (const auto& point : pointsOf(drawn)) {
                    ++seen;
                    const motion::Vec3 at {point.x, point.y, point.z};
                    onRim += std::abs(at.length() - radius) < .003 && std::abs(at.normalized().value().dot(eye.normalized().value()) - toward) < .002 ? 1 : 0;
                }
                expect(seen > 100, "it is drawn");
                expectEquals(onRim, seen, "every point of it on the ball's edge as seen");
                return length;
            };
            const auto whole = rim(false);
            const auto shown = rim(true);
            expect(shown > whole * .98, "and none of it is hidden by the ball itself");
        }

        beginTest("A surface asked to draw no lines leaves the beam dark");
        {
            const motion::PreparedComposition prepared(scene({{"edges", 0}}));
            std::atomic<bool> cancel {false};
            if (prepared.hiddenLines != nullptr) { motion::hidden::Baker::bakeAll(*prepared.hiddenLines, 48000, cancel); }
            motion::BeamRenderer beam;
            int lit = 0;
            for (std::int64_t index = 0; index < 1600; ++index) {
                const auto point = beam.sample(prepared, 1, index, 48000, false, 1);
                lit += point.r != 0 || point.g != 0 || point.b != 0 ? 1 : 0;
            }
            expectEquals(lit, 0, "every sample is dark, not left to take its clip's colour");
        }

        beginTest("An outline is drawn over patterns as well");
        {
            const motion::PreparedComposition prepared(scene({{"edges", 0}, {"outline", 100}, {"rings", 100}}, false, true));
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto eye = prepared.viewpoint(1)->position;
            const auto toward = radius / eye.length();
            int onRim = 0;
            for (const auto& point : pointsOf(frame->drawing(0))) {
                const motion::Vec3 at {point.x, point.y, point.z};
                onRim += std::abs(at.normalized().value().dot(eye.normalized().value()) - toward) < .002 ? 1 : 0;
            }
            expect(onRim > 300, "all round the ball: " + juce::String(onRim));
        }

        beginTest("A ball's outline reaches the far side of it from a mirror's view");
        {
            // Not hidden lines: what the eye sees of it is the same either way.
            const motion::PreparedComposition prepared(scene({{"edges", 0}, {"outline", 100}}, true));
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto eye = prepared.viewpoint(1)->position;
            int seen = 0, grazing = 0;
            for (const auto& point : pointsOf(reflectionOf(*frame, 1))) {
                ++seen;
                // Off the floor towards the ball, the line grazes it.
                auto direction = (motion::Vec3 {point.x, point.y, point.z} - eye).normalized().value();
                direction.y = -direction.y;
                const motion::Vec3 start {point.x, point.y, point.z};
                const auto toCentre = motion::Vec3 {} - start;
                const auto passes = (toCentre - direction * toCentre.dot(direction)).length();
                grazing += std::abs(passes - radius) < .02 ? 1 : 0;
            }
            expect(seen > 50, "the floor shows its outline");
            expect(grazing > seen * 9 / 10, "where its edge is, seen from the floor");
        }

        beginTest("Edges is how bright a surface's own lines are");
        {
            const auto brightest = [&](double edges) {
                const motion::PreparedComposition prepared(scene({{"edges", edges}, {"outline", 100}}));
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, 1, workspace);
                float own = 0;
                // Its own lines come first, before its outline.
                const auto* drawn = frame->drawing(0);
                for (std::size_t index = 0; drawn != nullptr && index < 8; ++index) {
                    const auto r = drawn->shape(index)->nextVector(.5f).r;
                    own = std::max(own, r < 0 ? 1.0f : r);
                }
                return own;
            };
            expect(std::abs(brightest(50) - .5f) < 1.0e-3f, "half as bright at 50");
            expect(std::abs(brightest(100) - 1) < 1.0e-3f, "as they are at 100");
        }

        beginTest("On an XY-only scope, light shows as the beam's time: the dim side is drawn faster");
        {
            // The share of the beam's time the lit top half of a ball takes,
            // lit from above.
            const auto topShare = [&](bool xyOnly) {
                auto project = scene({{"edges", 100}});
                addClip(project, generatorAsset(5, "directionalLight"), 50, {{"rotation.x", -60}, {"rotation.y", 0}});
                project.scope.xyOnly = xyOnly;
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, 1, workspace);
                const auto* drawn = frame->drawing(0);
                int top = 0, all = 0;
                for (int step = 0; drawn != nullptr && step < 4000; ++step) {
                    const auto point = drawn->sample((step + .5) / 4000);
                    ++all;
                    top += point.y > 0 ? 1 : 0;
                }
                return static_cast<double>(top) / std::max(1, all);
            };
            const auto withBrightness = topShare(false), xyOnly = topShare(true);
            expect(xyOnly > withBrightness + .02, "the lit half takes more of the beam with no brightness to show it by: "
                + juce::String(xyOnly, 3) + " against " + juce::String(withBrightness, 3));
        }

        beginTest("Patterns lie on the surface, and are lit as it is");
        {
            for (const auto* pattern : {"spiral", "noise", "slices", "rings"}) {
                auto project = scene({{"edges", 0}, {pattern, 100}});
                // Straight down: its underside is in its own shadow.
                addClip(project, generatorAsset(5, "directionalLight"), 50, {{"rotation.x", -90}, {"rotation.y", 0}, {"light.shadows", 100}});
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, 1, workspace);
                int seen = 0, onBall = 0, litTop = 0, top = 0, litBottom = 0;
                for (const auto& point : pointsOf(frame->drawing(0))) {
                    ++seen;
                    onBall += std::abs(motion::Vec3 {point.x, point.y, point.z}.length() - radius) < .02 ? 1 : 0;
                    if (point.y > radius * .5) {
                        ++top;
                        litTop += point.r > .3f ? 1 : 0;
                    }
                    litBottom += point.y < -radius * .5 && point.r > 0 ? 1 : 0;
                }
                const juce::String name(pattern);
                expect(seen > 100, name + " is drawn");
                expectEquals(onBall, seen, name + " lies on the ball");
                expect(top > 0 && litTop == top, name + " is lit on top");
                expectEquals(litBottom, 0, name + " is dark underneath");
            }
        }

        beginTest("A pattern is lit as the round surface it lies on, not its flat faces");
        {
            auto project = scene({{"edges", 0}, {"rings", 100}});
            addClip(project, generatorAsset(5, "directionalLight"), 50, {{"rotation.x", -90}, {"rotation.y", 0}, {"light.shadows", 100}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            auto points = pointsOf(frame->drawing(0));
            std::erase_if(points, [](const Seen& point) { return point.y < .1; });
            std::sort(points.begin(), points.end(), [](const Seen& a, const Seen& b) { return a.y < b.y; });
            float steepest = 0;
            for (std::size_t index = 1; index < points.size(); ++index) {
                if (points[index].y - points[index - 1].y < .002) { steepest = std::max(steepest, std::abs(points[index].r - points[index - 1].r)); }
            }
            expect(points.size() > 100, "it is drawn where the light falls");
            expect(steepest < .03f, "its light changes smoothly with its height: " + juce::String(steepest));
        }

        beginTest("Seen from another eye, only the near side of a pattern shows when the camera hides lines");
        {
            const auto farPoints = [&](bool hiding) {
                const motion::PreparedComposition prepared(scene({{"edges", 0}, {"spiral", 100}}, false, hiding));
                motion::hidden::Workspace workspace;
                const motion::Vec3 position {2.5, 1, 1};
                const auto forward = (motion::Vec3 {} - position).normalized().value();
                const auto right = forward.cross({0, 1, 0}).normalized().value();
                const motion::hidden::OtherEye other {{position, right, right.cross(forward), forward, 2}, {1.5, 1.5}};
                const auto frame = motion::hidden::cull(prepared, 1, workspace, nullptr, &other);
                int far = 0;
                for (const auto& point : pointsOf(frame->drawing(0))) {
                    const motion::Vec3 at {point.x, point.y, point.z};
                    // Beyond eight degrees past the rim: nearer it, lines lifted
                    // onto the round surface graze its flat faces, and on
                    // screen are within a percent of its radius of the rim.
                    far += at.normalized().value().dot((position - at).normalized().value()) < -.14 ? 1 : 0;
                }
                return far;
            };
            expect(farPoints(false) > 100, "with nothing hidden, all of it shows");
            expectEquals(farPoints(true), 0, "with lines hidden, none of its far side");
        }

        beginTest("A camera hatching over its frame shades by a curve's density: on the surfaces it sees, densest where lit");
        {
            auto project = scene({{"edges", 0}});
            project.cameras.front().properties["hatching"] = motion::Curve(100);
            project.cameras.front().properties["hatchDetail"] = motion::Curve(256);
            // From one side: that half of the ball is lit, the other in its own shadow.
            addClip(project, generatorAsset(5, "directionalLight"), 50, {{"rotation.x", 0}, {"rotation.y", 90}, {"light.shadows", 100}});
            // At 48 kHz, so the beam's cycle holds less than the curve would take.
            const motion::PreparedComposition prepared(project, 48000);
            expect(prepared.hiddenLines != nullptr, "a camera that hatches is baked");
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            expect(frame->hatching.size() == 1 && frame->hatching.front().clip == 0, "the ball is hatched");
            std::size_t points = 0, lit = 0, dark = 0, onBall = 0;
            for (const auto& entry : frame->hatching) {
                for (std::size_t index = 0; index < entry.drawing->shapeCount(); ++index) {
                    const auto point = entry.drawing->shape(index)->nextVector(0);
                    const motion::Vec3 at {point.x, point.y, point.z};
                    ++points;
                    // On its faces: a 16-sided ball's are this far in at most.
                    onBall += at.length() < radius + 1.0e-3 && at.length() > radius * .92 ? 1 : 0;
                    (at.x > 0 ? lit : dark) += 1;
                }
            }
            expect(points > 100, "it is drawn: " + juce::String(static_cast<int>(points)));
            expectEquals(static_cast<int>(onBall), static_cast<int>(points), "every point of it on the ball");
            expect(std::max(lit, dark) > 4 * std::min(lit, dark), "densest where lit: " + juce::String(static_cast<int>(lit)) + " on one side, " + juce::String(static_cast<int>(dark)) + " on the other");
            expect(points <= 48000 / 60 / 2, "no more than half a beam cycle's samples: " + juce::String(static_cast<int>(points)));
            // The beam draws it as part of the ball.
            std::atomic<bool> cancel {false};
            motion::hidden::Baker::bakeAll(*prepared.hiddenLines, 48000, cancel);
            motion::BeamRenderer beam;
            beam.planCycle(prepared, 1, 48000);
            bool drawn = false;
            for (const auto& segment : beam.plannedSegments()) { drawn = drawn || (segment.kind == motion::BeamRenderer::Kind::draw && segment.world && segment.clip == &prepared.clips[0]); }
            expect(drawn, "the beam draws the hatching");
        }

        beginTest("A mirror shows the scene, not hatching of its own, when the camera hatches");
        {
            auto project = scene({{"edges", 0}, {"reflects", 100}});
            project.cameras.front().properties["hatching"] = motion::Curve(100);
            addClip(project, generatorAsset(5, "directionalLight"), 50, {{"rotation.x", -90}, {"rotation.y", 0}, {"light.shadows", 0}});
            const motion::PreparedComposition prepared(project, 48000);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto hatchedOver = std::any_of(frame->hatching.begin(), frame->hatching.end(), [](const auto& entry) { return entry.clip == 0 && entry.drawing != nullptr && !entry.drawing->empty(); });
            const auto* drawn = frame->drawing(0);
            expect(!hatchedOver && (drawn == nullptr || drawn->empty()), "none on the mirror");
        }

        beginTest("A pattern's lines run on unbroken, even over a cone's tip");
        {
            motion::Project project;
            project.duration = 3;
            addClip(project, generatorAsset(1, "cone"), 10, {{"edges", 0}, {"slices", 100}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto* drawn = frame->drawing(0);
            int breaks = 0;
            for (std::size_t index = 1; drawn != nullptr && index < drawn->shapeCount(); ++index) {
                const auto a = drawn->shape(index - 1)->nextVector(1), b = drawn->shape(index)->nextVector(0);
                // Apart by more than a drawing takes as joined.
                breaks += std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y), static_cast<double>(a.z - b.z)) > 1.0e-6 ? 1 : 0;
            }
            expect(drawn != nullptr && drawn->shapeCount() > 100, "the slices are drawn: " + juce::String(drawn != nullptr ? static_cast<int>(drawn->shapeCount()) : 0));
            expect(breaks <= 8, "each slice one stroke: " + juce::String(breaks) + " breaks");
        }

        beginTest("Noise morphs as it flows, and comes back round after a whole turn");
        {
            const auto drawnAt = [&](double time, double phase) {
                motion::Project project;
                project.duration = 6;
                addClip(project, generatorAsset(1, "sphere"), 10, {{"edges", 0}, {"noise", 100}, {"noiseFlow", .5}, {"noisePhase", phase}});
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, time, workspace);
                std::vector<float> points;
                for (const auto& point : pointsOf(frame->drawing(0))) {
                    points.push_back(static_cast<float>(point.x));
                    points.push_back(static_cast<float>(point.y));
                }
                return points;
            };
            const auto alike = [](const std::vector<float>& one, const std::vector<float>& other) {
                if (one.size() != other.size()) { return false; }
                for (std::size_t index = 0; index < one.size(); ++index) {
                    if (std::abs(one[index] - other[index]) > 1.0e-4f) { return false; }
                }
                return true;
            };
            const auto start = drawnAt(.5, 0);
            expect(!start.empty(), "it draws");
            expect(!alike(start, drawnAt(1, 0)), "half a second on, it has morphed");
            expect(alike(start, drawnAt(2.5, 0)), "a turn later (2 s at half a turn a second) it is as it was");
            expect(alike(start, drawnAt(.5, 360)), "and a whole turn of phase is none");
        }

        beginTest("Noise eased to a stop has flowed as far as its flow carried it, never back");
        {
            motion::Curve flow;
            flow.setKey({0, 1, motion::Interpolation::linear});
            flow.setKey({2, 0, motion::Interpolation::linear});
            expectWithinAbsoluteError(motion::TransformPose::flowedBy(flow, 2), 1.0, 1.0e-9);
            expectWithinAbsoluteError(motion::TransformPose::flowedBy(flow, 3), 1.0, 1.0e-9);
            double previous = 0;
            bool forward = true;
            for (int step = 1; step <= 30; ++step) {
                const auto flowed = motion::TransformPose::flowedBy(flow, step * .1);
                forward = forward && flowed >= previous;
                previous = flowed;
            }
            expect(forward, "slowing, it still only goes forward");
            expectWithinAbsoluteError(motion::TransformPose::flowedBy(motion::Curve(.5), 4), 2.0, 1.0e-12);
        }
    }

private:
    static constexpr double radius = .5;

    // A ball at the middle, styled as asked, as a camera above and in front
    // sees it; over a mirror floor if asked.
    static motion::Project scene(std::initializer_list<std::pair<const char*, double>> style, bool withFloor = false, bool hiding = false) {
        motion::Project project;
        project.duration = 3;
        std::vector<std::pair<const char*, double>> ball {{"generator.radius", radius}};
        ball.insert(ball.end(), style.begin(), style.end());
        addClip(project, generatorAsset(1, "sphere"), 10, ball);
        if (withFloor) { addClip(project, generatorAsset(2, "grid"), 20, {{"rotation.x", -90}, {"position.y", -.8}, {"generator.width", 3}, {"generator.height", 3}, {"reflects", 100}}); }
        motion::Camera camera;
        camera.id = 40;
        camera.hideHiddenLines = hiding;
        camera.properties["position.y"] = motion::Curve(1.5);
        camera.properties["position.z"] = motion::Curve(4);
        camera.properties["rotation.x"] = motion::Curve(-20);
        project.cameras.push_back(camera);
        return project;
    }

    static const motion::PreparedDrawing* reflectionOf(const motion::hidden::Frame& frame, std::size_t clip) {
        for (const auto& entry : frame.reflections) {
            if (entry.clip == clip) { return entry.drawing.get(); }
        }
        return nullptr;
    }

    // A drawing's points every so often along each line, with their red.
    struct Seen {
        double x, y, z;
        float r;
    };
    static std::vector<Seen> pointsOf(const motion::PreparedDrawing* drawing) {
        std::vector<Seen> points;
        for (std::size_t index = 0; drawing != nullptr && index < drawing->shapeCount(); ++index) {
            for (int step = 0; step <= 4; ++step) {
                const auto point = drawing->shape(index)->nextVector(step / 4.0f);
                points.push_back({point.x, point.y, point.z, point.r});
            }
        }
        return points;
    }

    static std::shared_ptr<motion::Asset> generatorAsset(motion::Id id, const char* kind) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->name = motion::findGenerator(kind)->name.data();
        asset->extension = ".generator";
        asset->data.append(kind, std::strlen(kind));
        const auto decoded = motion::decodeAsset(*asset);
        jassert(decoded.wasOk());
        return asset;
    }

    static void addClip(motion::Project& project, std::shared_ptr<motion::Asset> asset, motion::Id id, const std::vector<std::pair<const char*, double>>& properties) {
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(id, *asset, 0);
        clip.duration = 3;
        // Square to the scene and white, unless asked otherwise.
        for (const auto* name : {"rotation.x", "rotation.y", "rotation.z"}) { clip.properties[name] = motion::Curve(0); }
        for (const auto* name : {"red", "green", "blue"}) { clip.properties[name] = motion::Curve(1); }
        for (const auto& [name, value] : properties) { clip.properties[name] = motion::Curve(value); }
        motion::Track track;
        track.id = id + 1;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
    }
};

static MotionLineStyleTest motionLineStyleTest;
