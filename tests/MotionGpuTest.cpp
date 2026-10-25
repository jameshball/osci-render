#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/gpu/GpuChain.h"
#include "../Source/motion/render/HiddenLines.h"
#include "../Source/motion/render/gpu/GpuCrossings.h"
#include "../Source/motion/render/gpu/GpuScene.h"
#include <cstring>
#include <random>

// The GPU bends points as the CPU does: every effect, on a clip in a turned
// group, copied by a Duplicator, under an effect on the whole composition.
// Single precision on the GPU, so within a small tolerance; an effect that
// snaps to steps (Bit crush, Polygon, Spiral crush) may land a point either
// side of one where the two round it differently, so a few such are allowed.
// Without a GPU (a machine with none, or OSCI_MOTION_GPU=off) it says so and
// checks nothing, unless one was asked for (OSCI_MOTION_GPU=force).
class MotionGpuTest : public juce::UnitTest {
public:
    MotionGpuTest() : juce::UnitTest("Motion GPU", "Motion") {}

    void runTest() override {
        beginTest("The GPU, when there is one, passes its first check");
        auto* device = motion::gpu::Device::shared();
        if (device == nullptr) {
            // Asked for (OSCI_MOTION_GPU=force), there must be one.
            expect(motion::gpu::mode() != motion::gpu::Mode::force, "a GPU was asked for, but: " + motion::gpu::Device::unavailableReason());
            logMessage("No GPU: " + motion::gpu::Device::unavailableReason());
            return;
        }
        logMessage("GPU: " + device->describe());

        for (const auto& definition : motion::effectCatalog()) {
            beginTest("Points bend on the GPU as on the CPU through " + juce::String(definition.name));
            const auto project = projectWith(definition);
            const motion::PreparedComposition composition(project);
            expect(!composition.clips.empty(), "the project has clips");
            std::mt19937 random(7);
            std::uniform_real_distribution<float> spread(-1.5f, 1.5f), shade(0.0f, 1.0f);
            for (const double time : {0.0, .7, 1.3}) {
                motion::gpu::ChainBatch batch;
                std::vector<std::pair<const motion::PreparedClip*, osci::Point>> sources;
                for (const auto& clip : composition.clips) {
                    if (!clip.active(time) || clip.light) { continue; }
                    const auto program = batch.program(composition, clip, time);
                    for (int index = 0; index < 400; ++index) {
                        // Some uncoloured, as most sources' points are.
                        const auto coloured = index % 3 != 0;
                        const osci::Point point(spread(random), spread(random), spread(random), coloured ? shade(random) : -1.0f, coloured ? shade(random) : -1.0f, coloured ? shade(random) : -1.0f);
                        batch.add(program, point);
                        sources.emplace_back(&clip, point);
                    }
                }
                const auto bent = batch.run(*device);
                expect(bent.has_value() && bent->size() == sources.size(), "the GPU bends every point");
                if (!bent.has_value() || bent->size() != sources.size()) { continue; }
                int differ = 0;
                double worst = 0;
                for (std::size_t index = 0; index < sources.size(); ++index) {
                    const auto expected = composition.worldPoint(*sources[index].first, sources[index].second, time);
                    const auto& got = (*bent)[index];
                    if (expected.has_value() != got.has_value()) {
                        ++differ;
                        continue;
                    }
                    if (!expected.has_value()) { continue; }
                    const auto error = (*got - *expected).length();
                    worst = std::max(worst, error);
                    differ += error > 2.0e-4 * std::max(1.0, expected->length()) ? 1 : 0;
                }
                const auto steps = definition.kind == motion::EffectKind::bitCrush || definition.kind == motion::EffectKind::polygon || definition.kind == motion::EffectKind::spiralCrush;
                const auto allowed = steps ? static_cast<int>(sources.size() / 50) : 0;
                logMessage(juce::String(definition.name) + " at " + juce::String(time) + " s: " + juce::String(static_cast<int>(sources.size())) + " points, " + juce::String(differ) + " differ, worst by " + juce::String(worst, 7));
                expect(differ <= allowed, juce::String(differ) + " of " + juce::String(static_cast<int>(sources.size())) + " points differ at " + juce::String(time) + " s (worst by " + juce::String(worst, 6) + ")");
            }
        }

        beginTest("Surfaces cut through each other on the GPU just as on the CPU");
        {
            // Random triangles of several surfaces in front of the eye, some
            // copied exactly onto another surface (lying in each other's
            // planes, which single precision can't tell from crossing), some
            // copied mirrored. The GPU only finds the pairs that may cross;
            // tested exactly, they make the very runs the CPU makes.
            std::mt19937 random(11);
            std::uniform_real_distribution<double> spread(-1, 1), depth(3, 6), small(-.15, .15);
            std::vector<motion::hidden::crossing::Triangle> triangles;
            for (std::uint32_t surface = 0; surface < 6; ++surface) {
                for (int index = 0; index < 400; ++index) {
                    const motion::Vec3 centre {spread(random), spread(random), depth(random)};
                    motion::hidden::crossing::Triangle triangle;
                    triangle.surface = surface;
                    for (auto& corner : triangle.eye) { corner = centre + motion::Vec3 {small(random), small(random), small(random)}; }
                    triangles.push_back(triangle);
                }
                if (surface > 0) {
                    // Some of the last surface's, exactly, and mirrored in x.
                    for (int index = 0; index < 60; ++index) {
                        auto copy = triangles[(surface - 1) * 520 + static_cast<std::size_t>(index)];
                        copy.surface = surface;
                        triangles.push_back(copy);
                        for (auto& corner : copy.eye) { corner.x = -corner.x; }
                        triangles.push_back(copy);
                    }
                }
                while (triangles.size() < (surface + 1) * 520) { triangles.push_back(triangles.back()); triangles.back().surface = surface; }
            }
            const auto cpu = motion::hidden::crossing::runs(triangles, 1, {});
            const auto gpu = motion::gpu::crossingRuns(*device, triangles, 1, {});
            expect(gpu.has_value(), "the GPU finds pairs");
            const auto flat = [](const std::vector<std::vector<motion::hidden::crossing::Piece>>& runs) {
                std::vector<std::array<double, 7>> pieces;
                for (const auto& run : runs) {
                    for (const auto& piece : run) { pieces.push_back({static_cast<double>(piece.triangle), piece.eyeFrom.x, piece.eyeFrom.y, piece.eyeFrom.z, piece.eyeTo.x, piece.eyeTo.y, piece.eyeTo.z}); }
                }
                return pieces;
            };
            expect(!cpu.empty(), "the surfaces cross");
            expect(gpu.has_value() && flat(*gpu) == flat(cpu), "into the same runs, piece for piece (" + juce::String(static_cast<int>(flat(cpu).size())) + " pieces on the CPU, " + juce::String(gpu.has_value() ? static_cast<int>(flat(*gpu).size()) : -1) + " on the GPU)");
        }

        beginTest("Surfaces are drawn into depth on the GPU as on the CPU, and the solid ones that show found");
        {
            // Two hundred triangles of four surfaces, crossing and hiding each
            // other in front of an eye, some of a vast floor cut by its near plane.
            std::mt19937 random(11);
            std::uniform_real_distribution<double> across(-2.5, 2.5), deep(-0.5, 6), size(-0.6, 0.6);
            const motion::hidden::Eye eye {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            const motion::hidden::Projection projection {1.2};
            std::vector<motion::gpu::Scene::Triangle> flat;
            std::vector<std::array<motion::Vec3, 3>> placed;
            for (std::uint32_t index = 0; index < 200; ++index) {
                // Every tenth a vast floor's, just under the eye and through
                // its near plane, reaching far past the frame once cut there.
                const motion::Vec3 centre {across(random), across(random), deep(random)};
                const auto offset = [&] { return motion::Vec3 {size(random), size(random), size(random)}; };
                const auto floor = index % 10 == 0;
                const auto side = across(random) * 40;
                const std::array<motion::Vec3, 3> corners = floor ? std::array<motion::Vec3, 3> {motion::Vec3 {side, -0.3 + size(random) * .1, -0.99}, motion::Vec3 {side + 80, -0.3, 60}, motion::Vec3 {side - 80, -0.3, 60}} : std::array<motion::Vec3, 3> {centre + offset(), centre + offset(), centre + offset()};
                const std::array<osci::Point, 3> sources {osci::Point(0, 0, 0), osci::Point(1, 0, 0), osci::Point(0, 1, 0)};
                flat.push_back(motion::gpu::Scene::triangle(corners, sources, index % 4, index, motion::gpu::Scene::solid));
                placed.push_back(corners);
            }
            // On the CPU: drawn a triangle at a time, those that show gathered.
            motion::hidden::DepthBuffer cpuDepth;
            cpuDepth.cover({});
            std::vector<motion::hidden::crossing::Triangle> cpuTriangles;
            std::vector<std::array<osci::Point, 3>> cpuSources;
            for (std::uint32_t index = 0; index < placed.size(); ++index) {
                cpuDepth.tagNext(std::uint64_t {index % 4} << 32 | index);
                const auto& c = placed[index];
                motion::hidden::drawFlat(projection, {}, cpuDepth, {eye.see(c[0]), eye.see(c[1]), eye.see(c[2])}, {osci::Point(0, 0, 0), osci::Point(1, 0, 0), osci::Point(0, 1, 0)}, false, [&](const std::array<motion::Vec3, 3>& corners, const std::array<osci::Point, 3>& from) {
                    cpuTriangles.push_back({corners, index % 4});
                    cpuSources.push_back(from);
                });
            }
            motion::hidden::dropUnseen(cpuTriangles, cpuSources, cpuDepth, projection.focalLength);
            // On the GPU, all at once.
            motion::hidden::DepthBuffer gpuDepth;
            gpuDepth.cover({});
            const motion::gpu::Scene::View camera {eye, projection, {}, &gpuDepth, true};
            motion::gpu::Scene::Frame frame;
            frame.flat = &flat;
            frame.camera = &camera;
            frame.gather = true;
            const auto drawn = motion::gpu::Scene::draw(*device, frame);
            expect(drawn.has_value(), "the GPU draws them");
            if (drawn.has_value()) {
                // Whether points all over the frame, at all depths, are hidden.
                std::uniform_real_distribution<double> onScreen(-1.05, 1.05), inverse(0.05, 1.5);
                int differ = 0, hidden = 0;
                constexpr int points = 20000;
                for (int index = 0; index < points; ++index) {
                    const motion::hidden::Screen point {onScreen(random), onScreen(random), inverse(random)};
                    const auto onCpu = cpuDepth.hiddenBy(point).has_value(), onGpu = gpuDepth.hiddenBy(point).has_value();
                    differ += onCpu != onGpu;
                    hidden += onCpu;
                }
                logMessage("Depth: " + juce::String(differ) + " of " + juce::String(points) + " points differ (" + juce::String(hidden) + " hidden)");
                // Points on the floor itself, as lines drawn on it are: none is
                // hidden by the floor, on either.
                std::uniform_real_distribution<double> share(0.0, 1.0);
                int onFloor = 0, floorDiffer = 0;
                for (std::uint32_t index = 0; index < placed.size(); index += 10) {
                    const auto& c = placed[index];
                    for (int sample = 0; sample < 400; ++sample) {
                        auto s = share(random), t = share(random);
                        if (s + t > 1) {
                            s = 1 - s;
                            t = 1 - t;
                        }
                        const auto at = projection(eye.see(c[0] + (c[1] - c[0]) * s + (c[2] - c[0]) * t));
                        if (!at.has_value() || std::abs(at->x) > 1 || std::abs(at->y) > 1) { continue; }
                        ++onFloor;
                        const auto onCpu = cpuDepth.hiddenBy(*at), onGpu = gpuDepth.hiddenBy(*at);
                        floorDiffer += onCpu.has_value() != onGpu.has_value();
                    }
                }
                logMessage("On the floor: " + juce::String(floorDiffer) + " of " + juce::String(onFloor) + " points differ");
                expect(onFloor > 200 && floorDiffer <= onFloor / 200, "points on a vast floor cut at the near plane are hidden alike (" + juce::String(floorDiffer) + " differ)");
                expect(hidden > points / 10, "much of the frame is hidden");
                expect(differ <= points / 200, "points are hidden alike, but for a few at edges (" + juce::String(differ) + " differ)");
                const auto cpuCount = static_cast<double>(cpuTriangles.size()), gpuCount = static_cast<double>(drawn->triangles.size());
                logMessage("Gathered: " + juce::String(cpuTriangles.size()) + " on the CPU, " + juce::String(drawn->triangles.size()) + " on the GPU");
                expect(cpuCount > 50 && std::abs(gpuCount - cpuCount) <= .01 * cpuCount + 1, "the same solid triangles show");
                expect(drawn->triangles.size() == drawn->sources.size(), "each with its points before the chain");
            }
        }

        beginTest("Lines are lit on the GPU as on the CPU");
        {
            // A solid box, twisted so its faces are diced, over a solid grid,
            // lit by a point light and a spot that cast shadows, and ambient.
            motion::Project project;
            project.duration = 3;
            addClip(project, generatorAsset(1, "box"), 10, {{"rotation.y", 20}, {"solid", 100}});
            project.tracks.change(0).clips.front().effects.push_back(motion::makeEffect(70, *motion::effectDefinition("twist")));
            addClip(project, generatorAsset(2, "grid"), 20, {{"rotation.x", -90}, {"position.y", -.8}, {"scale.x", 3}, {"scale.y", 3}, {"solid", 100}});
            addClip(project, generatorAsset(32, "pointLight"), 30, {{"position.x", 1}, {"position.y", 1.5}, {"light.range", 6}});
            addClip(project, generatorAsset(42, "spotLight"), 40, {{"position.x", -1.5}, {"position.y", 2}, {"rotation.x", -60}});
            addClip(project, generatorAsset(52, "ambientLight"), 50, {{"light.intensity", .2}});
            motion::Camera camera;
            camera.id = 60;
            camera.properties["position.y"] = motion::Curve(1.5);
            camera.properties["rotation.x"] = motion::Curve(-20);
            project.cameras.push_back(camera);
            const motion::PreparedComposition composition(project);
            for (const double time : {0.0, 1.5}) {
                motion::hidden::Workspace onCpu, onGpu;
                onCpu.gpu = false;
                const auto cpu = motion::hidden::cull(composition, time, onCpu);
                const auto gpu = motion::hidden::cull(composition, time, onGpu);
                expect(cpu != nullptr && gpu != nullptr, "both are culled");
                if (cpu == nullptr || gpu == nullptr) { continue; }
                expectEquals(static_cast<int>(gpu->drawings.size()), static_cast<int>(cpu->drawings.size()), "the same clips are drawn");
                for (std::size_t index = 0; index < std::min(cpu->drawings.size(), gpu->drawings.size()); ++index) {
                    const auto [cpuLength, cpuLight] = measure(*cpu->drawings[index].drawing);
                    const auto [gpuLength, gpuLight] = measure(*gpu->drawings[index].drawing);
                    // The GPU cuts and draws the surfaces itself (see
                    // gpu::Tessellation), a little differently: as long within 2%.
                    expectWithinAbsoluteError(gpuLength, cpuLength, .02 * cpuLength + 1.0e-6, "a clip's lines are as long at " + juce::String(time) + " s");
                    expectWithinAbsoluteError(gpuLight, cpuLight, .02 * cpuLight + 1.0e-3, "and as bright");
                }
            }
        }
    }

private:
    // A drawing's length, and its length weighted by how bright it is drawn
    // along it, from points every so often along each line.
    static std::pair<double, double> measure(const motion::PreparedDrawing& drawing) {
        double length = 0, light = 0;
        for (std::size_t index = 0; index < drawing.shapeCount(); ++index) {
            auto* shape = drawing.shape(index);
            if (shape == nullptr) { continue; }
            const auto whole = static_cast<double>(shape->length());
            length += whole;
            for (int step = 0; step < 8; ++step) {
                const auto point = shape->nextVector((static_cast<float>(step) + .5f) / 8);
                light += whole / 8 * std::max({point.r, point.g, point.b});
            }
        }
        return {length, light};
    }

    static std::shared_ptr<motion::Asset> generatorAsset(motion::Id id, const char* kind) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->name = motion::findGenerator(kind)->name.data();
        asset->extension = ".generator";
        asset->data.append(kind, std::strlen(kind));
        motion::decodeAsset(*asset);
        return asset;
    }

    static void addClip(motion::Project& project, std::shared_ptr<motion::Asset> asset, motion::Id id, std::initializer_list<std::pair<const char*, double>> properties) {
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(id, *asset, 0);
        clip.duration = 3;
        for (const auto& [name, value] : properties) { clip.properties[name] = motion::Curve(value); }
        motion::Track track;
        track.id = id + 1;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
    }

    // A cube with `definition`'s effect (its values away from their
    // defaults, so it acts), copied three times, in a turned and scaled
    // group, under a Twist on the whole composition.
    static motion::Project projectWith(const motion::EffectDefinition& definition) {
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
        clip.properties["scale.x"] = motion::Curve(.8);
        clip.properties["position.y"] = motion::Curve(.25);
        auto effect = motion::makeEffect(40, definition);
        for (std::size_t index = 1; index < definition.parameters.size(); ++index) {
            const auto& parameter = definition.parameters[index];
            // Two thirds of the way from its least to its most, unless that
            // is a whole count (segments, copies), which keeps its default.
            if (parameter.decimals == 0) { continue; }
            effect.properties[parameter.id] = motion::Curve(parameter.min + (parameter.max - parameter.min) * 2 / 3);
        }
        clip.effects.push_back(effect);
        if (definition.kind != motion::EffectKind::duplicator) { clip.effects.push_back(motion::makeEffect(41, *motion::effectDefinition("duplicator"))); }
        motion::Group group;
        group.id = 50;
        group.properties["rotation.z"] = motion::Curve(25);
        group.properties["scale.y"] = motion::Curve(1.2);
        project.groups.push_back(group);
        motion::Track track;
        track.id = 10;
        track.group = group.id;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
        project.effects.push_back(motion::makeEffect(60, *motion::effectDefinition("twist")));
        motion::Camera camera;
        camera.id = 30;
        project.cameras.push_back(camera);
        return project;
    }
};

static MotionGpuTest motionGpuTest;
