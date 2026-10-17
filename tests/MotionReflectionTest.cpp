#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/render/HiddenLineBaker.h"
#include "../Source/motion/render/HiddenLines.h"

class MotionReflectionTest : public juce::UnitTest {
public:
    MotionReflectionTest() : juce::UnitTest("Motion reflections", "Motion") {}

    void runTest() override {
        beginTest("A mirror floor shows what stands over it where its reflection is seen");
        {
            auto project = scene(100);
            const motion::PreparedComposition prepared(project);
            expect(prepared.hiddenLines != nullptr, "something reflecting is baked, with no lights and no camera hiding lines");
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto* shown = reflectionOf(*frame, floorClip);
            expect(shown != nullptr && !shown->empty(), "the floor shows the box");
            const auto eye = prepared.viewpoint(1)->position;
            int seen = 0, onFloor = 0, inside = 0, towardBox = 0;
            for (const auto& point : pointsOf(shown)) {
                ++seen;
                onFloor += std::abs(point.y - floorHeight) < 1.0e-4 && std::abs(point.x) <= 1.5 + 1.0e-4 && std::abs(point.z) <= 1.5 + 1.0e-4 ? 1 : 0;
                // Its outline is its own edge; inside it, the eye's line to a
                // point, turned back up off the floor, meets the box.
                if (std::abs(std::abs(point.x) - 1.5) < 1.0e-4 || std::abs(std::abs(point.z) - 1.5) < 1.0e-4) { continue; }
                ++inside;
                auto direction = (point - eye).normalized().value();
                direction.y = -direction.y;
                towardBox += meetsBox(point, direction, {0, boxHeight, 0}, boxSize / 2 + 1.0e-3) ? 1 : 0;
            }
            expect(inside > 50, "along its lines");
            expectEquals(onFloor, seen, "every point of the reflection is on the floor");
            expectEquals(towardBox, inside, "and inside its edge what it shows is the box");
        }

        beginTest("Seen from another eye (an editor's view), mirrors show what they would to it");
        {
            const motion::PreparedComposition prepared(scene(100));
            motion::hidden::Workspace workspace;
            // Off to one side and higher than the camera, looking at the middle.
            const motion::Vec3 position {2.5, 2, 2.5};
            const auto forward = (motion::Vec3 {} - position).normalized().value();
            const auto right = forward.cross({0, 1, 0}).normalized().value();
            const motion::hidden::OtherEye other {{position, right, right.cross(forward), forward, 2}, {1.5, 1.5}};
            const auto frame = motion::hidden::cull(prepared, 1, workspace, nullptr, &other);
            int seen = 0, towardBox = 0;
            for (const auto& point : pointsOf(reflectionOf(*frame, floorClip))) {
                if (std::abs(std::abs(point.x) - 1.5) < 1.0e-4 || std::abs(std::abs(point.z) - 1.5) < 1.0e-4) { continue; }
                ++seen;
                auto direction = (point - position).normalized().value();
                direction.y = -direction.y;
                towardBox += meetsBox(point, direction, {0, boxHeight, 0}, boxSize / 2 + 1.0e-3) ? 1 : 0;
            }
            expect(seen > 20, "the floor shows the box to it");
            expectEquals(towardBox, seen, "where that eye would see it");
            expect(frame->drawing(0) == nullptr, "and nothing is hidden from it: the box draws whole");
        }

        beginTest("Reflects is how bright what a mirror shows is, and how much its own lines give way");
        {
            const auto brightness = [&](double reflects) {
                const motion::PreparedComposition prepared(scene(reflects));
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, 1, workspace);
                float reflected = 0, own = 0;
                for (const auto& point : pointsOf(reflectionOf(*frame, floorClip))) { reflected = std::max(reflected, point.r); }
                const auto* drawn = frame->drawing(floorClip);
                for (std::size_t index = 0; drawn != nullptr && index < drawn->shapeCount(); ++index) {
                    const auto point = drawn->shape(index)->nextVector(.5f);
                    own = std::max(own, point.r < 0 ? 1.0f : point.r);
                }
                return std::pair {reflected, drawn != nullptr && drawn->empty() ? 0.0f : drawn == nullptr ? 1.0f : own};
            };
            const auto full = brightness(100), half = brightness(50), none = brightness(0);
            expect(std::abs(half.first - full.first * .5f) < 1.0e-3f, "half as bright at 50 as at 100");
            expect(none.first == 0, "nothing shows at 0");
            expectEquals(none.second, 1.0f, "at 0 its own lines are as they are");
            expect(std::abs(half.second - .5f) < 1.0e-3f, "at 50 its own lines are half as bright");
            expectEquals(full.second, 0.0f, "at 100 it shows only the scene");
        }

        beginTest("What stands between a mirror and a line hides the line in the mirror");
        {
            // A low bar behind the box, and a block that comes between the
            // floor and the bar where the eye sees the bar in the floor.
            const auto nearby = [&](bool blocked) {
                auto project = scene(100, false);
                addClip(project, generatorAsset(5, "box"), 50, {{"position.y", -.6}, {"position.z", -1.5}, {"generator.width", .6}, {"generator.height", .02}, {"generator.depth", .02}, {"green", 0}, {"blue", 0}});
                if (blocked) { addClip(project, generatorAsset(6, "box"), 60, {{"position.y", -.7}, {"position.z", -1.28}, {"generator.width", .2}, {"generator.height", .2}, {"generator.depth", .2}}); }
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                const auto frame = motion::hidden::cull(prepared, 1, workspace);
                // Where the bar's middle is seen in the floor.
                const motion::hidden::FlatMirror floor {{0, 1, 0}, floorHeight, {}};
                const auto seen = floor.reflectionOf(prepared.viewpoint(1)->position, {0, -.6, -1.5}).value();
                int count = 0;
                // The bar's points, red where the block's are white.
                for (const auto& point : pointsOf(reflectionOf(*frame, floorClip))) { count += point.r > 0 && point.g == 0 && (point - seen).length() < .04 ? 1 : 0; }
                return count;
            };
            expect(nearby(false) > 0, "the bar is seen in the floor");
            expectEquals(nearby(true), 0, "but not where the block stands between them");
        }

        beginTest("A mirror shows where surfaces cut through each other");
        {
            // A small box poking out of the big one's corner: where it comes
            // out is a line neither box draws.
            auto project = scene(100);
            addClip(project, generatorAsset(8, "box"), 80, {{"position.x", .3}, {"position.y", .1}, {"position.z", .3}, {"generator.width", .3}, {"generator.height", .3}, {"generator.depth", .3}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto eye = prepared.viewpoint(1)->position;
            // On the big box's front face (z = .3), facing the camera, where the small one's bottom face (y = -.05) meets it.
            const motion::Vec3 from {.15, -.05, .3}, to {.3, -.05, .3};
            int along = 0;
            for (const auto& point : pointsOf(reflectionOf(*frame, floorClip))) {
                auto direction = (point - eye).normalized().value();
                direction.y = -direction.y;
                const motion::Vec3 start {point.x, point.y, point.z};
                // How near the line off the floor passes to the crossing.
                const auto edge = to - from;
                const auto across = direction.cross(edge);
                if (!(across.length() > 1.0e-9)) { continue; }
                const auto gap = std::abs((from - start).dot(across)) / across.length();
                const auto t = (from - start).cross(direction).dot(across) / across.dot(across);
                along += gap < .004 && t > .2 && t < .8 ? 1 : 0;
            }
            expect(along > 0, "the crossing is seen in the floor");
        }

        beginTest("Where a mirror and a surface cut through each other, the line is drawn on the surface");
        {
            // The small box poking out of the big one, which is now a mirror
            // drawing none of its own lines.
            auto project = scene(0);
            project.tracks.change(0).clips[0].properties["reflects"] = motion::Curve(100);
            addClip(project, generatorAsset(8, "box"), 80, {{"position.x", .3}, {"position.y", .1}, {"position.z", .3}, {"generator.width", .3}, {"generator.height", .3}, {"generator.depth", .3}, {"solid", 100}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto onMirror = static_cast<int>(pointsOf(frame->drawing(0)).size());
            int along = 0;
            // On the big box's front face, where the small one's left side
            // comes out of it, in the small box's own terms (it stands at .3, .1, .3).
            for (const auto& point : pointsOf(frame->drawing(2))) { along += std::abs(point.x + .15) < .002 && std::abs(point.z) < .002 && point.y > -.1 && point.y < .1 ? 1 : 0; }
            expect(along > 0, "the crossing is drawn with the small box's lines");
        }

        beginTest("A mirror ball shows the scene on its surface, inside its round rim");
        {
            auto project = scene(0);
            addClip(project, generatorAsset(7, "sphere"), 70, {{"position.x", 1.1}, {"position.y", -.4}, {"generator.radius", .4}, {"reflects", 100}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, 1, workspace);
            const auto ballClip = prepared.clips.size() - 1;
            const auto* shown = reflectionOf(*frame, ballClip);
            expect(shown != nullptr && !shown->empty(), "the ball shows the box and the floor");
            int seen = 0, onBall = 0;
            for (const auto& point : pointsOf(shown)) {
                ++seen;
                // Within the little its points are joined across, straight.
                onBall += std::abs((point - motion::Vec3 {1.1, -.4, 0}).length() - .4) < 2.0e-3 ? 1 : 0;
            }
            expectEquals(onBall, seen, "every point of it is on the ball");
            const auto* own = frame->drawing(ballClip);
            expect(own != nullptr && own->empty(), "a ball that reflects wholly draws none of its own lines");
        }

        beginTest("The beam draws what mirrors show as part of the mirror");
        {
            const motion::PreparedComposition prepared(scene(100));
            std::atomic<bool> cancel {false};
            motion::hidden::Baker::bakeAll(*prepared.hiddenLines, 48000, cancel);
            motion::BeamRenderer beam;
            beam.planCycle(prepared, 1, 48000);
            bool reflected = false;
            for (const auto& segment : beam.plannedSegments()) {
                if (segment.kind == motion::BeamRenderer::Kind::draw && segment.world) { reflected = segment.clip == &prepared.clips[floorClip]; }
            }
            expect(reflected, "the floor's reflections are planned as its own");
        }
    }

private:
    static constexpr std::size_t floorClip = 1;
    static constexpr double floorHeight = -.8, boxHeight = .1, boxSize = .6;

    // A box over a mirror floor 3 across, as a camera above and in front sees
    // them, without lights; the floor reflecting as much as asked.
    static motion::Project scene(double reflects, bool withBox = true) {
        motion::Project project;
        project.duration = 3;
        if (withBox) {
            addClip(project, generatorAsset(1, "box"), 10, {{"position.y", boxHeight}, {"generator.width", boxSize}, {"generator.height", boxSize}, {"generator.depth", boxSize}, {"solid", 100}});
        } else {
            addClip(project, generatorAsset(1, "box"), 10, {{"position.x", 9}, {"solid", 100}});
        }
        addClip(project, generatorAsset(2, "grid"), 20, {{"rotation.x", -90}, {"position.y", floorHeight}, {"generator.width", 3}, {"generator.height", 3}, {"reflects", reflects}, {"solid", 100}});
        motion::Camera camera;
        camera.id = 40;
        camera.properties["position.y"] = motion::Curve(1.5);
        camera.properties["position.z"] = motion::Curve(4);
        camera.properties["rotation.x"] = motion::Curve(-25);
        project.cameras.push_back(camera);
        return project;
    }

    static const motion::PreparedDrawing* reflectionOf(const motion::hidden::Frame& frame, std::size_t clip) {
        for (const auto& entry : frame.reflections) {
            if (entry.clip == clip) { return entry.drawing.get(); }
        }
        return nullptr;
    }

    // A reflection's points, in the scene, every so often along each line,
    // with their red and green.
    struct Seen {
        double x, y, z;
        float r, g;
        motion::Vec3 operator-(motion::Vec3 other) const { return motion::Vec3 {x, y, z} - other; }
    };
    static std::vector<Seen> pointsOf(const motion::PreparedDrawing* drawing) {
        std::vector<Seen> points;
        for (std::size_t index = 0; drawing != nullptr && index < drawing->shapeCount(); ++index) {
            for (int step = 0; step <= 8; ++step) {
                const auto point = drawing->shape(index)->nextVector(step / 8.0f);
                points.push_back({point.x, point.y, point.z, point.r, point.g});
            }
        }
        return points;
    }

    // Whether a ray meets a cube of half-size `half` round `centre`.
    static bool meetsBox(const Seen& from, motion::Vec3 direction, motion::Vec3 centre, double half) {
        double enter = 0, leave = 1.0e9;
        const std::array<double, 3> start {from.x, from.y, from.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto low = centre[axis] - half, high = centre[axis] + half, step = direction[axis];
            if (std::abs(step) < 1.0e-12) {
                if (start[axis] < low || start[axis] > high) { return false; }
                continue;
            }
            const auto a = (low - start[axis]) / step, b = (high - start[axis]) / step;
            enter = std::max(enter, std::min(a, b));
            leave = std::min(leave, std::max(a, b));
        }
        return enter <= leave;
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

    static void addClip(motion::Project& project, std::shared_ptr<motion::Asset> asset, motion::Id id, std::initializer_list<std::pair<const char*, double>> properties) {
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

static MotionReflectionTest motionReflectionTest;
