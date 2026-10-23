#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/BeamRenderer.h"

// Times the Motion pipeline on a deliberately heavy project: preparation,
// per-sample beam rendering (the audio thread's work), editing with undo,
// and saving and loading. Set MOTION_BENCHMARK_PROJECT to a path to also
// write the project there, for profiling the app with it.
class MotionPerformanceBenchmark : public juce::UnitTest {
public:
    MotionPerformanceBenchmark() : juce::UnitTest("Motion performance benchmark", "Motion Benchmark") {}

    static juce::String sphere(int rings, int segments) {
        juce::String obj;
        for (int ring = 0; ring <= rings; ++ring) {
            const auto theta = juce::MathConstants<double>::pi * ring / rings;
            for (int segment = 0; segment < segments; ++segment) {
                const auto phi = juce::MathConstants<double>::twoPi * segment / segments;
                obj << "v " << std::sin(theta) * std::cos(phi) << " " << std::cos(theta) << " " << std::sin(theta) * std::sin(phi) << "\n";
            }
        }
        for (int ring = 0; ring < rings; ++ring) {
            for (int segment = 0; segment < segments; ++segment) {
                const auto a = ring * segments + segment + 1, b = ring * segments + (segment + 1) % segments + 1;
                obj << "f " << a << " " << b << " " << b + segments << " " << a + segments << "\n";
            }
        }
        return obj;
    }

    // 16 tracks of two animated, effected spheres each, half of them in
    // groups, with routed oscillators, two cut cameras and a keyed Scope.
    static motion::Project heavyProject(motion::Document& document) {
        motion::Project project;
        project.duration = 60;
        auto asset = std::make_shared<motion::Asset>();
        asset->id = document.newId(); asset->name = "sphere.obj"; asset->extension = ".obj";
        const auto obj = sphere(24, 32);
        asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
        motion::decodeAsset(*asset);
        project.assets = {asset};
        motion::Modulator wave;
        wave.id = document.newId();
        wave.shape.rateHz = 0.5;
        project.modulators = {wave};
        for (int index = 0; index < 4; ++index) {
            motion::Group group;
            group.id = document.newId();
            group.name = "Group " + std::to_string(index);
            group.properties["rotation.y"].setKey({0, 0, motion::Interpolation::linear});
            group.properties["rotation.y"].setKey({60, 360, motion::Interpolation::linear});
            project.groups.push_back(group);
        }
        for (int index = 0; index < 16; ++index) {
            motion::Track track;
            track.id = document.newId();
            track.name = "Track " + std::to_string(index);
            if (index % 2 == 0) { track.group = project.groups[static_cast<std::size_t>(index / 4)].id; }
            for (int part = 0; part < 2; ++part) {
                auto clip = motion::Document::makeClip(document.newId(), *asset, part * 30.0);
                clip.duration = 30;
                for (int key = 0; key < 8; ++key) {
                    clip.properties["position.x"].setKey({key * 4.0, std::sin(key + index) * 0.5, motion::Interpolation::smooth});
                    clip.properties["rotation.z"].setKey({key * 4.0, key * 45.0, motion::Interpolation::cubic});
                }
                clip.properties["scale.x"].base = clip.properties["scale.y"].base = 0.2;
                clip.effects.push_back(motion::makeEffect(document.newId(), *motion::effectDefinition("rotate")));
                clip.effects.push_back(motion::makeEffect(document.newId(), *motion::effectDefinition("wobble")));
                project.routes.push_back({document.newId(), wave.id, clip.id, "position.y", 0.25, motion::ModulationMode::add});
                track.insert(clip, project.tempo());
            }
            project.tracks.push_back(track);
        }
        for (int index = 0; index < 2; ++index) {
            motion::Camera camera;
            camera.id = document.newId();
            camera.properties["position.x"].setKey({0, -1.0 + index * 2, motion::Interpolation::smooth});
            camera.properties["position.x"].setKey({60, 1.0 - index * 2, motion::Interpolation::smooth});
            project.cameras.push_back(camera);
            project.cameraCuts.push_back({document.newId(), camera.id, index * 30.0, 30.0});
        }
        project.beam.properties["intensity"].setKey({0, 3, motion::Interpolation::smooth});
        project.beam.properties["intensity"].setKey({60, 7, motion::Interpolation::smooth});
        project.routes.push_back({document.newId(), wave.id, project.beam.id, "hue", 90, motion::ModulationMode::add});
        return project;
    }

    template <typename Work>
    static double milliseconds(Work&& work) {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        work();
        return juce::Time::getMillisecondCounterHiRes() - start;
    }

    void runTest() override {
        beginTest("Heavy project");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(heavyProject(document));
        constexpr double rate = 48000;

        std::unique_ptr<motion::PreparedComposition> prepared;
        const auto preparation = milliseconds([&] { prepared = std::make_unique<motion::PreparedComposition>(document.project(), rate); });
        expect(prepared->preparationError.isEmpty(), prepared->preparationError);
        logMessage("Prepare composition: " + juce::String(preparation, 1) + " ms");

        // Two seconds (MOTION_BENCHMARK_SECONDS) from the middle, as the audio
        // thread renders it.
        motion::BeamRenderer beam;
        const auto seconds = juce::jmax(1, juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_SECONDS", "2").getIntValue());
        const int samples = seconds * static_cast<int>(rate);
        double checksum = 0;
        const auto rendering = milliseconds([&] {
            for (int sample = 0; sample < samples; ++sample) {
                const auto index = static_cast<std::int64_t>(29 * rate) + sample;
                const auto point = beam.sample(*prepared, static_cast<double>(index) / rate, index, rate, true, 1);
                checksum += point.x + point.y;
            }
        });
        expect(std::isfinite(checksum));
        const auto perSample = rendering * 1.0e6 / samples;
        logMessage("Render: " + juce::String(perSample, 1) + " ns per sample, " + juce::String(1000.0 * samples / rate / rendering, 1) + "x real time");

        const auto beamValues = milliseconds([&] {
            for (int block = 0; block < 10000; ++block) { checksum += prepared->beam.at(block * 0.01)[0]; }
        });
        logMessage("Scope beam values: " + juce::String(beamValues * 1000.0 / 10000, 2) + " us per block");

        const auto edits = milliseconds([&] {
            for (int edit = 0; edit < 50; ++edit) {
                document.edit("Move", [edit](motion::Project& project) { project.tracks.change(0).clips.front().properties["position.x"].base = edit; });
            }
        });
        const auto undos = milliseconds([&] { for (int edit = 0; edit < 50; ++edit) { undo.undo(); } });
        logMessage("Edit: " + juce::String(edits / 50, 2) + " ms each; undo: " + juce::String(undos / 50, 2) + " ms each");

        std::unique_ptr<juce::XmlElement> saved;
        const auto saving = milliseconds([&] { saved = std::make_unique<juce::XmlElement>(document.save()); });
        juce::UndoManager reloadUndo;
        motion::Document reloaded(reloadUndo);
        const auto loading = milliseconds([&] { expect(reloaded.load(*saved).wasOk()); });
        logMessage("Save: " + juce::String(saving, 1) + " ms; load: " + juce::String(loading, 1) + " ms");

        const auto destination = juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_PROJECT", {});
        if (destination.isNotEmpty()) {
            juce::XmlElement project("motion-project");
            project.setAttribute("schema", 1);
            project.addChildElement(new juce::XmlElement(*saved));
            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary(project, data);
            expect(juce::File(destination).replaceWithData(data.getData(), data.getSize()));
            logMessage("Wrote " + destination);
        }
    }
};

static MotionPerformanceBenchmark motionPerformanceBenchmark;
