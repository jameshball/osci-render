#include <JuceHeader.h>
#include "../Source/motion/export/SignalExporter.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/BeamRenderer.h"

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
        for (const auto& [name, scale] : {std::pair<const char*, Scale> {"typical", {8, 20, 8, 1, 4}}, {"dense", {32, 60, 24, 3, 16}}}) {
            beginTest(juce::String("Stress project: ") + name);
            benchmarkProject(name, stressProject(scale));
        }
        beginTest("Nested compositions");
        benchmarkProject("nested", nestedProject());
        beginTest("MIDI chords");
        benchmarkRender("midi", midiProject());
        beginTest("Imports");
        benchmarkImports();
    }

private:
    struct Scale { int tracks, clipsPerTrack, keysPerCurve, effectsPerClip, modulators; };
    static constexpr double sampleRate = 48000, renderSeconds = 10;

    template <typename Function>
    static double milliseconds(Function&& function, int repeats = 1) {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int index = 0; index < repeats; ++index) { function(); }
        return (juce::Time::getMillisecondCounterHiRes() - start) / repeats;
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
                for (const auto* property : {"position.x", "position.y", "rotation.z", "scale.x", "red", "weight"}) {
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

    // Sixteen-note chords on every sixteenth for the whole render.
    static motion::Project midiProject() {
        auto project = stressProject({1, 1, 2, 0, 0});
        auto& clip = project.tracks[0].clips[0];
        clip.duration = renderSeconds;
        std::vector<motion::MidiNote> notes;
        motion::Id id = 1;
        for (int step = 0; step < static_cast<int>(renderSeconds * 8); ++step) {
            for (int voice = 0; voice < 16; ++voice) { notes.push_back({id++, step * 0.25, 0.5, 48 + voice * 2, 100, 1}); }
        }
        clip.midi = motion::MidiNotes::create(std::move(notes)).source;
        project.duration = renderSeconds;
        return project;
    }

    void benchmarkProject(const juce::String& name, const motion::Project& project) {
        report(name + ": prepare for signal", milliseconds([&] { const motion::PreparedComposition prepared(project, sampleRate); }, 3), "ms");
        report(name + ": prepare editor geometry", milliseconds([&] { const motion::PreparedComposition prepared(project, sampleRate, nullptr, motion::CompositionPurpose::editorGeometry); }, 3), "ms");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(project);
        juce::String saved;
        report(name + ": save", milliseconds([&] { saved = document.save().toString(); }), "ms");
        report(name + ": saved size", saved.getNumBytesAsUTF8() / 1024.0, "KiB");
        const auto xml = juce::XmlDocument::parse(saved);
        motion::Project loaded;
        report(name + ": load", milliseconds([&] {
            const auto result = motion::Document::prepareLoad(*xml, loaded);
            expect(result.wasOk(), result.getErrorMessage());
        }), "ms");
        const auto clip = document.project().tracks.empty() || document.project().tracks[0].clips.empty() ? motion::Id(0) : document.project().tracks[0].clips[0].id;
        int step = 0;
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
