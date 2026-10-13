#include <JuceHeader.h>
#include "../Source/motion/export/SignalExporter.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/render/HiddenLineBaker.h"
#include "../Source/motion/render/HiddenLines.h"
#if JUCE_MAC
#include <malloc/malloc.h>
#endif

// Timings for Motion's heavy paths on synthetic stress projects. Opt-in, as
// timings mean little in Debug or under a sanitizer: set MOTION_BENCHMARK=1
// and run the Release test binary with the MotionPerformance category.
class MotionPerformanceTest : public juce::UnitTest {
public:
    MotionPerformanceTest() : juce::UnitTest("Motion performance", "MotionPerformance") {}

    void runTest() override {
        if (juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK", "").isEmpty()) {
            logMessage("Motion benchmarks not run: set MOTION_BENCHMARK=1.");
            return;
        }
        // Only hidden lines, when picking them out by name.
        if (juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_ONLY", "").isEmpty()) {
            for (const auto& [name, scale] : {std::pair<const char*, Scale> {"typical", {8, 20, 8, 1, 4}}, {"dense", {32, 60, 24, 3, 16}}}) {
                beginTest(juce::String("Stress project: ") + name);
                benchmarkProject(name, stressProject(scale));
            }
            beginTest("Nested compositions");
            benchmarkProject("nested", nestedProject());
            beginTest("Imports");
            benchmarkImports();
        }
        beginTest("Hidden lines");
        benchmarkHiddenLines();
    }

private:
    struct Scale { int tracks, clipsPerTrack, keysPerCurve, effectsPerClip, modulators; };

    // A mesh from the repository's models, turning once over two seconds.
    static std::shared_ptr<motion::Asset> modelAsset(motion::Id id, const char* model) {
        const auto file = juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("Resources/models").getChildFile(model);
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->name = model;
        asset->extension = ".obj";
        file.loadFileAsData(asset->data);
        const auto decoded = motion::decodeAsset(*asset);
        jassert(decoded.wasOk());
        return asset;
    }
    // `set`: properties each copy has, as they are.
    static motion::Project hiddenLineProject(const std::vector<std::shared_ptr<motion::Asset>>& assets, int copies, bool bulge, bool still = false, const std::map<std::string, double>& set = {}) {
        motion::Project project;
        project.duration = 2;
        project.assets.assign(assets.begin(), assets.end());
        motion::Id next = 1000;
        for (int copy = 0; copy < copies; ++copy) {
            const auto& asset = *assets[static_cast<std::size_t>(copy) % assets.size()];
            auto clip = motion::Document::makeClip(next++, asset, 0);
            clip.duration = 2;
            clip.properties["rotation.x"] = motion::Curve(20 + 7 * copy);
            auto& turn = clip.properties["rotation.y"];
            turn.setKey({0, 37.0 * copy, motion::Interpolation::linear});
            if (!still) { turn.setKey({2, 37.0 * copy + 360, motion::Interpolation::linear}); }
            // Copies overlap, so their surfaces cut through each other.
            clip.properties["position.x"] = motion::Curve(copies == 1 ? 0 : -.45 + .9 * copy / (copies - 1));
            if (bulge) { clip.effects.push_back(motion::makeEffect(next++, *motion::effectDefinition("bulge"))); }
            for (const auto& [property, value] : set) { clip.properties[property] = motion::Curve(value); }
            motion::Track track;
            track.id = next++;
            track.insert(clip, project.tempo());
            project.tracks.push_back(track);
        }
        motion::Camera camera;
        camera.id = next++;
        camera.hideHiddenLines = true;
        project.cameras.push_back(camera);
        return project;
    }
    // A generator's clip on a track of its own, playing as the teapots do.
    static void addGenerator(motion::Project& project, const char* kind, motion::Id id, std::initializer_list<std::pair<const char*, double>> properties) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->name = motion::findGenerator(kind)->name.data();
        asset->extension = ".generator";
        asset->data.append(kind, std::strlen(kind));
        const auto decoded = motion::decodeAsset(*asset);
        jassert(decoded.wasOk());
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(id + 1, *asset, 0);
        clip.duration = 2;
        for (const auto& [name, value] : properties) { clip.properties[name] = motion::Curve(value); }
        motion::Track track;
        track.id = id + 2;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
    }
    // Per frame on one thread, then every frame on all of them, and what was
    // kept (drawn, reflected and hatched): the same project must keep the
    // same lines however fast it gets.
    void benchmarkHiddenLineProject(const juce::String& name, const motion::Project& project) {
        // MOTION_BENCHMARK_ONLY=teapot runs only the scenes with that in their name.
        const auto only = juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_ONLY", "");
        if (only.isNotEmpty() && !name.containsIgnoreCase(only)) { return; }
        const motion::PreparedComposition prepared(project, sampleRate);
        if (prepared.hiddenLines == nullptr) {
            logMessage(name + ": no camera hides hidden lines");
            return;
        }
        motion::hidden::Workspace depth;
        constexpr int frames = 40;
        double kept = 0;
        std::size_t shapes = 0;
        // Frames are sampled where something plays.
        double first = prepared.duration, last = 0;
        for (const auto& clip : prepared.clips) {
            first = std::min(first, clip.start);
            last = std::max(last, std::min(clip.end, prepared.duration));
        }
        if (!(last > first)) { first = 0, last = prepared.duration; }
        // MOTION_BENCHMARK_REPEAT=20 runs the frames over, long enough to profile.
        const auto repeats = std::max(1, juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_REPEAT", "1").getIntValue());
        const auto perFrame = milliseconds([&] {
            for (int frame = 0; frame < frames; ++frame) {
                const auto culled = motion::hidden::cull(prepared, first + (last - first) * frame / frames, depth);
                for (const auto* entries : {&culled->drawings, &culled->reflections, &culled->hatching}) {
                    for (const auto& entry : *entries) {
                        kept += entry.drawing->length();
                        shapes += entry.drawing->shapeCount();
                    }
                }
            }
        }, repeats) / frames;
        kept /= repeats;
        shapes /= static_cast<std::size_t>(repeats);
        std::atomic<bool> stop {false};
        const auto bake = milliseconds([&] { motion::hidden::Baker::bakeAll(*prepared.hiddenLines, sampleRate, stop); });
        report("Hidden lines: " + name + " per frame", perFrame, "ms");
        report("Hidden lines: " + name + " bake of " + juce::String(static_cast<int>(prepared.hiddenLines->frameCount())) + " frames", bake, "ms");
        logMessage("  kept " + juce::String(kept, 6) + " in " + juce::String(static_cast<int>(shapes)) + " shapes over " + juce::String(frames) + " frames");
    }
    void benchmarkHiddenLines() {
        const auto teapot = modelAsset(1, "teapot.obj"), lamp = modelAsset(2, "lamp.obj");
        benchmarkHiddenLineProject("teapot", hiddenLineProject({teapot}, 1, false));
        benchmarkHiddenLineProject("lamp", hiddenLineProject({lamp}, 1, false));
        benchmarkHiddenLineProject("bulged teapot", hiddenLineProject({teapot}, 1, true));
        benchmarkHiddenLineProject("four crossing teapots", hiddenLineProject({teapot}, 4, false));
        benchmarkHiddenLineProject("two bulged crossing teapots", hiddenLineProject({teapot}, 2, true));
        benchmarkHiddenLineProject("still bulged teapot", hiddenLineProject({teapot}, 1, true, true));
        // Faint edges and rings laid over it, as a style draws them.
        benchmarkHiddenLineProject("styled teapot", hiddenLineProject({teapot}, 1, false, false, {{"edges", 40}, {"rings", 60}}));
        // Noise flowing as it plays: its contours worked out afresh each frame.
        benchmarkHiddenLineProject("flowing noise teapot", hiddenLineProject({teapot}, 1, false, false, {{"edges", 0}, {"noise", 100}, {"noiseFlow", 1}}));
        // Lit by a point light above and in front, casting shadows.
        auto lit = hiddenLineProject({teapot}, 1, false);
        addGenerator(lit, "pointLight", 5000, {{"position.y", 1.5}, {"position.z", 1}, {"light.range", 6}});
        benchmarkHiddenLineProject("lit teapot", lit);
        // Beside a mirror ball, which shows it.
        auto mirrored = hiddenLineProject({teapot}, 1, false);
        addGenerator(mirrored, "sphere", 5000, {{"position.x", 1.1}, {"scale.x", .5}, {"scale.y", .5}, {"scale.z", .5}, {"reflects", 100}});
        benchmarkHiddenLineProject("mirrored teapot", mirrored);
        // Still, with the ball cutting into it: what stands between and where
        // they cross are found once.
        auto stillMirrored = hiddenLineProject({teapot}, 1, false, true);
        addGenerator(stillMirrored, "sphere", 5000, {{"position.x", .6}, {"scale.x", .5}, {"scale.y", .5}, {"scale.z", .5}, {"reflects", 100}});
        benchmarkHiddenLineProject("still mirrored teapot", stillMirrored);
        // Lit, and shaded by the camera's hatching rather than brightness.
        auto hatched = lit;
        hatched.cameras.front().properties["hatching"] = motion::Curve(100);
        benchmarkHiddenLineProject("hatched teapot", hatched);
        // A project of your own: MOTION_BENCHMARK_PROJECT=/path/to.osci-motion
        const auto path = juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_PROJECT", "");
        if (path.isEmpty()) { return; }
        juce::MemoryBlock bytes;
        juce::File(path).loadFileAsData(bytes);
        const auto xml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), static_cast<int>(bytes.getSize()));
        const auto* composition = xml != nullptr ? xml->getChildByName("composition") : nullptr;
        motion::Project project;
        if (composition == nullptr || motion::Document::prepareLoad(*composition, project).failed()) {
            logMessage("Could not load " + path);
            return;
        }
        benchmarkHiddenLineProject(juce::File(path).getFileNameWithoutExtension(), project);
    }
    static constexpr double sampleRate = 48000, renderSeconds = 10;

    template <typename Function>
    static double milliseconds(Function&& function, int repeats = 1) {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int index = 0; index < repeats; ++index) { function(); }
        return (juce::Time::getMillisecondCounterHiRes() - start) / repeats;
    }
    // Heap in use, where the platform reports it.
    static double heapMebibytes() {
       #if JUCE_MAC
        malloc_statistics_t statistics;
        malloc_zone_statistics(nullptr, &statistics);
        return static_cast<double>(statistics.size_in_use) / (1024 * 1024);
       #else
        return 0;
       #endif
    }
    void report(const juce::String& scenario, double value, const juce::String& unit) {
        logMessage(scenario.paddedRight(' ', 44) + juce::String(value, 2).paddedLeft(' ', 10) + " " + unit);
    }

    // A closed wavy ring as an SVG file, or `frames` of it turning as GPLA.
    static std::shared_ptr<motion::Asset> drawnAsset(motion::Id id, int frames, int segments) {
        const auto point = [segments](int step, int frame) {
            const auto angle = juce::MathConstants<double>::twoPi * step / segments + 0.05 * frame;
            const auto radius = 0.5 + 0.1 * std::sin(7 * angle);
            return std::pair {radius * std::cos(angle), radius * std::sin(angle)};
        };
        juce::String text;
        if (frames == 1) {
            text << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-1 -1 2 2\"><path d=\"M";
            for (int step = 0; step <= segments; ++step) { text << (step == 0 ? "" : " L") << point(step, 0).first << " " << point(step, 0).second; }
            text << "\"/></svg>";
        } else {
            text << "{\"frames\":[";
            for (int frame = 0; frame < frames; ++frame) {
                text << (frame == 0 ? "" : ",") << "{\"focalLength\":1,\"objects\":[{\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],\"vertices\":[[";
                for (int step = 0; step <= segments; ++step) {
                    text << (step == 0 ? "" : ",") << "{\"x\":" << point(step, frame).first << ",\"y\":" << point(step, frame).second << ",\"z\":-1}";
                }
                text << "]]}]}";
            }
            text << "]}";
        }
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->name = "Shape " + juce::String(id);
        asset->extension = frames == 1 ? ".svg" : ".gpla";
        asset->data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
        const auto decoded = motion::decodeAsset(*asset);
        jassert(decoded.wasOk());
        return asset;
    }

    // Every track holds a run of clips with keyed transforms and colour,
    // effects, and modulators routed across them.
    static motion::Project stressProject(Scale scale) {
        motion::Project project;
        project.duration = scale.clipsPerTrack * 2.0 + 1;
        motion::Id next = 1;
        const auto still = drawnAsset(next++, 1, 256), animated = drawnAsset(next++, 30, 512);
        project.assets = {still, animated};
        const std::array<const char*, 5> effects {"wobble", "twist", "polygon", "bitCrush", "perspective"};
        std::vector<std::pair<motion::Id, std::string>> routable;
        for (int trackIndex = 0; trackIndex < scale.tracks; ++trackIndex) {
            motion::Track track;
            track.id = next++;
            track.name = "Track " + std::to_string(trackIndex + 1);
            for (int clipIndex = 0; clipIndex < scale.clipsPerTrack; ++clipIndex) {
                auto clip = motion::Document::makeClip(next++, clipIndex % 2 == 0 ? *still : *animated, clipIndex * 2.0);
                clip.duration = 1.8;
                for (const auto* property : {"position.x", "position.y", "rotation.z", "scale.x", "red", "opacity"}) {
                    auto& curve = clip.properties[property];
                    for (int key = 0; key < scale.keysPerCurve; ++key) {
                        const auto time = 1.8 * key / std::max(1, scale.keysPerCurve - 1);
                        curve.setKey({time, 0.5 + 0.4 * std::sin(key + trackIndex), motion::Interpolation::smooth});
                    }
                }
                for (int effect = 0; effect < scale.effectsPerClip; ++effect) {
                    clip.effects.push_back(motion::makeEffect(next++, *motion::effectDefinition(effects[static_cast<std::size_t>(effect) % effects.size()])));
                }
                routable.emplace_back(clip.id, "position.y");
                track.insert(std::move(clip), project.tempo());
            }
            project.tracks.push_back(std::move(track));
        }
        for (int index = 0; index < scale.modulators; ++index) {
            motion::Modulator modulator;
            modulator.id = next++;
            modulator.shape.waveform = static_cast<motion::ModulationWaveform>(index % 4);
            modulator.shape.rateHz = 0.5 + index;
            project.modulators.push_back(modulator);
            for (std::size_t route = static_cast<std::size_t>(index); route < routable.size(); route += static_cast<std::size_t>(scale.modulators)) {
                project.routes.push_back({next++, modulator.id, routable[route].first, routable[route].second, 0.1, motion::ModulationMode::add});
            }
        }
        return project;
    }

    // Four levels of reusable compositions, each instancing the one below
    // four times: 256 leaf clips from one 4x4 definition.
    static motion::Project nestedProject() {
        auto project = stressProject({4, 4, 8, 1, 0});
        motion::Id next = 100000;
        auto leaf = std::make_shared<motion::CompositionDefinition>();
        static_cast<motion::Composition&>(*leaf) = static_cast<const motion::Composition&>(project);
        leaf->id = next++;
        leaf->duration = 10;
        project.definitions.push_back(leaf);
        auto below = std::shared_ptr<const motion::CompositionDefinition>(leaf);
        for (int level = 0; level < 3; ++level) {
            auto definition = std::make_shared<motion::CompositionDefinition>();
            definition->id = next++;
            definition->duration = 10;
            for (int instance = 0; instance < 4; ++instance) {
                motion::Track track;
                track.id = next++;
                track.insert(motion::Document::makeCompositionClip(next++, *below, 0), project.tempo());
                definition->tracks.push_back(std::move(track));
            }
            project.definitions.push_back(definition);
            below = definition;
        }
        project.tracks.clear();
        project.duration = 10;
        motion::Track root;
        root.id = next++;
        root.insert(motion::Document::makeCompositionClip(next++, *below, 0), project.tempo());
        project.tracks.push_back(std::move(root));
        return project;
    }

    void benchmarkProject(const juce::String& name, const motion::Project& project) {
        {
            const auto before = heapMebibytes();
            const motion::PreparedComposition signal(project, sampleRate);
            const auto prepared = heapMebibytes();
            const motion::PreparedComposition geometry(project, sampleRate, nullptr, motion::CompositionPurpose::editorGeometry);
            report(name + ": memory, prepared for signal", prepared - before, "MiB");
            report(name + ": memory, prepared for the editor", heapMebibytes() - prepared, "MiB");
            const auto copied = heapMebibytes();
            const auto snapshot = project;
            report(name + ": memory, one project snapshot", heapMebibytes() - copied, "MiB");
        }
        report(name + ": prepare for signal", milliseconds([&] { const motion::PreparedComposition prepared(project, sampleRate); }, 3), "ms");
        report(name + ": prepare editor geometry", milliseconds([&] { const motion::PreparedComposition prepared(project, sampleRate, nullptr, motion::CompositionPurpose::editorGeometry); }, 3), "ms");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(project);
        juce::String saved;
        report(name + ": save", milliseconds([&] { saved = document.save().toString(); }), "ms");
        report(name + ": saved size", saved.getNumBytesAsUTF8() / 1024.0, "KiB");
        writeProjectFile(name, document);
        const auto xml = juce::XmlDocument::parse(saved);
        motion::Project loaded;
        report(name + ": load", milliseconds([&] {
            const auto result = motion::Document::prepareLoad(*xml, loaded);
            expect(result.wasOk(), result.getErrorMessage());
        }), "ms");
        const auto clip = document.project().tracks.empty() || document.project().tracks[0].clips.empty() ? motion::Id(0) : document.project().tracks[0].clips[0].id;
        int step = 0;
        {
            const auto before = heapMebibytes();
            for (int edit = 0; edit < 20; ++edit) {
                document.edit("Benchmark", [&](motion::Project& edited) {
                    auto* curve = motion::findPropertyCurve(edited, clip, "position.x");
                    if (curve != nullptr) { curve->base = 0.01 * ++step; }
                });
            }
            report(name + ": memory, 20 undo steps (one key each)", heapMebibytes() - before, "MiB");
        }
        report(name + ": edit (one key, with undo)", milliseconds([&] {
            document.edit("Benchmark", [&](motion::Project& edited) {
                auto* curve = motion::findPropertyCurve(edited, clip, "position.x");
                if (curve != nullptr) { curve->base = 0.01 * ++step; }
            });
        }, 50), "ms");
        auto preview = document.project();
        report(name + ": preview (one drag step)", milliseconds([&] { document.preview(preview); }, 50), "ms");
        auto window = project;
        window.duration = std::min(project.duration, renderSeconds);
        benchmarkRender(name, window);
    }

    // With MOTION_BENCHMARK_PROJECTS set to a folder, each stress project is
    // also written there as a project file, to profile in the app.
    static void writeProjectFile(const juce::String& name, const motion::Document& document) {
        const juce::File folder(juce::SystemStats::getEnvironmentVariable("MOTION_BENCHMARK_PROJECTS", ""));
        if (!folder.isDirectory()) { return; }
        juce::XmlElement project("motion-project");
        project.setAttribute("schema", 1);
        project.addChildElement(new juce::XmlElement(document.save()));
        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary(project, data);
        folder.getChildFile(name + ".osci-motion").replaceWithData(data.getData(), data.getSize());
    }

    void benchmarkRender(const juce::String& name, const motion::Project& project) {
        const motion::PreparedComposition prepared(project, sampleRate);
        expect(prepared.preparationError.isEmpty(), prepared.preparationError);
        juce::TemporaryFile file(".wav");
        const std::atomic<bool> cancel {false};
        const auto elapsed = milliseconds([&] { expect(motion::SignalExporter::write(prepared, file.getFile(), sampleRate, cancel).wasOk()); });
        report(name + ": render " + juce::String(project.duration, 0) + " s of signal", elapsed, "ms");
        report(name + ": render speed", project.duration * 1000 / elapsed, "x real time");
    }

    void benchmarkImports() {
        const auto decode = [this](const juce::String& name, const juce::String& extension, const juce::String& text, motion::BakeSettings bake = {}) {
            motion::Asset asset;
            asset.id = 1;
            asset.name = name + extension;
            asset.extension = extension;
            asset.bakeSettings = bake;
            asset.data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
            report("import " + name, milliseconds([&] { expect(motion::decodeAsset(asset).wasOk()); }), "ms");
        };
        juce::String svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 1000\">";
        juce::Random random(1);
        for (int path = 0; path < 2000; ++path) {
            svg << "<path d=\"M" << random.nextInt(1000) << " " << random.nextInt(1000);
            for (int curve = 0; curve < 4; ++curve) {
                svg << " C" << random.nextInt(1000) << " " << random.nextInt(1000) << " " << random.nextInt(1000) << " " << random.nextInt(1000) << " " << random.nextInt(1000) << " " << random.nextInt(1000);
            }
            svg << "\"/>";
        }
        svg << "</svg>";
        decode("SVG, 2000 paths", ".svg", svg);
        juce::String text;
        for (int line = 0; line < 40; ++line) { text << "The quick brown fox jumps over the lazy dog " << line << "\n"; }
        decode("text, 40 lines", ".txt", text);
        juce::String frames = "{\"frames\":[";
        for (int frame = 0; frame < 120; ++frame) {
            frames << (frame == 0 ? "" : ",") << "{\"focalLength\":1,\"objects\":[{\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],\"vertices\":[";
            for (int stroke = 0; stroke < 50; ++stroke) {
                frames << (stroke == 0 ? "" : ",") << "[";
                for (int vertex = 0; vertex < 10; ++vertex) {
                    frames << (vertex == 0 ? "" : ",") << "{\"x\":" << random.nextFloat() - 0.5f << ",\"y\":" << random.nextFloat() - 0.5f << ",\"z\":-1}";
                }
                frames << "]";
            }
            frames << "]}]}";
        }
        frames << "]}";
        decode("GPLA JSON, 120 frames", ".gpla", frames);
        motion::BakeSettings bake;
        bake.duration = 10;
        bake.frameRate = 30;
        decode("Lua bake, 300 frames", ".lua", "return {math.sin(step * 0.01 + phase * 6.28), math.cos(phase * 6.28), 0}", bake);
    }
};

static MotionPerformanceTest motionPerformanceTest;
