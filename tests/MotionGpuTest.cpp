#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/gpu/GpuChain.h"
#include "../Source/motion/render/HiddenLines.h"
#include <cstring>
#include <random>

// The GPU bends points as the CPU does: every effect, on a clip in a turned
// group, copied by a Duplicator, under an effect on the whole composition.
// Single precision on the GPU, so within a small tolerance; an effect that
// snaps to steps (Bit crush, Polygon, Spiral crush) may land a point either
// side of one where the two round it differently, so a few such are allowed.
// Without a GPU (a machine with none, or OSCI_MOTION_GPU=off) it says so and
// checks nothing.
class MotionGpuTest : public juce::UnitTest {
public:
    MotionGpuTest() : juce::UnitTest("Motion GPU", "Motion") {}

    void runTest() override {
        beginTest("The GPU, when there is one, passes its first check");
        auto* device = motion::gpu::Device::shared();
        if (device == nullptr) {
            logMessage("No GPU: " + motion::gpu::Device::unavailableReason());
            expect(true);
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
                    expectWithinAbsoluteError(gpuLength, cpuLength, .01 * cpuLength + 1.0e-6, "a clip's lines are as long at " + juce::String(time) + " s");
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
