#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/HiddenLines.h"

// Generators: sources made from a few numbers, which each clip keeps as its
// own properties, keyed and modulated like any other.
class MotionGeneratorsTest : public juce::UnitTest {
public:
    MotionGeneratorsTest() : juce::UnitTest("Motion generators", "Motion") {}

    void runTest() override {
        beginTest("A generator's drawing for values seen before is the one already made");
        {
            const auto& sphere = motion::generatorNamed("sphere");
            std::vector<double> values;
            for (const auto& spec : sphere.parameters) { values.push_back(spec.defaultValue); }
            auto& cache = motion::generator::DrawingCache::shared();
            const auto first = cache.at(sphere, values);
            expect(first == cache.at(sphere, values), "the same values give the same drawing");
            values.front() += 1;
            expect(first != cache.at(sphere, values), "other values give another");
            expect(first->bytes() > first->shapeCount() * sizeof(void*), "a drawing reckons the memory it holds");
            // A source has a surface (for Surface's lines and Mirror) where its drawings have faces.
            const auto& arc = motion::generatorNamed("arc");
            std::vector<double> arcValues;
            for (const auto& spec : arc.parameters) { arcValues.push_back(spec.defaultValue); }
            expect(motion::PreparedSource({first}, 30.0).hasFaces(), "a sphere has a surface");
            expect(!motion::PreparedSource({cache.at(arc, arcValues)}, 30.0).hasFaces(), "an arc has none");
        }

        beginTest("A generator decodes at its defaults, and its clip holds its parameters");
        {
            const auto star = generatorAsset(1, "star");
            expect(star->source != nullptr && star->source->frameCount() == 1, "a star draws one frame");
            const auto* drawing = star->source->drawingAt(0);
            expect(drawing != nullptr && drawing->shapeCount() == 10, "five points: ten edges");
            expect(!drawing->faces().empty() && drawing->faces().planar, "its inside hides what is behind it");
            const auto clip = motion::Document::makeClip(2, *star, 0);
            for (const auto& spec : motion::findGenerator("star")->parameters) {
                const auto found = clip.properties.find(std::string(spec.id));
                expect(found != clip.properties.end() && found->second.base == spec.defaultValue, juce::String(spec.label.data()) + " starts at its default");
            }
            auto unknown = std::make_shared<motion::Asset>();
            unknown->extension = ".generator";
            unknown->data.append("teapot", 6);
            expect(motion::decodeAsset(*unknown).failed(), "an unknown generator does not decode");
        }

        beginTest("A clip draws its own values, and a keyed value redraws it frame by frame");
        {
            auto project = generatorProject("star");
            auto& points = project.tracks.change(0).clips[0].properties["generator.points"];
            points.setKey({0, 5, motion::Interpolation::linear});
            points.setKey({1, 9, motion::Interpolation::linear});
            const motion::PreparedComposition prepared(project);
            const auto& source = *prepared.clips.front().source;
            expect(source.frameCount() > 1, "a keyed generator has a frame for each frame of its clip");
            const auto shapesAt = [&](double seconds) { return static_cast<int>(source.drawingAt(source.frameIndex(seconds))->shapeCount()); };
            expectEquals(shapesAt(0), 10, "five points at the start");
            expectEquals(shapesAt(1.5), 18, "nine points once the key is reached");
            expect(source.drawingAt(source.frameIndex(1.2)) == source.drawingAt(source.frameIndex(1.8)), "held values share one drawing");

            auto still = generatorProject("star");
            still.tracks.change(0).clips[0].properties["generator.points"] = motion::Curve(7);
            const motion::PreparedComposition plain(still);
            expectEquals(static_cast<int>(plain.clips.front().source->frameCount()), 1, "a clip whose values never change has one frame");
            expectEquals(static_cast<int>(plain.clips.front().source->drawingAt(0)->shapeCount()), 14, "drawn from its own value");
        }

        beginTest("A modulator drives a generator's parameter");
        {
            auto project = generatorProject("polygon");
            motion::Modulator modulator;
            modulator.id = 40;
            modulator.shape.rateHz = 1;
            project.modulators.push_back(modulator);
            project.routes.push_back({41, modulator.id, project.tracks[0].clips[0].id, "generator.sides", 3, motion::ModulationMode::add});
            const motion::PreparedComposition prepared(project);
            const auto& source = *prepared.clips.front().source;
            std::set<int> sides;
            for (int step = 0; step < 20; ++step) { sides.insert(static_cast<int>(source.drawingAt(source.frameIndex(step * .05))->shapeCount())); }
            expect(sides.size() > 2, "the polygon gains and loses sides as the modulator swings");
        }

        beginTest("Generator parameters save and load with their keys, and are named as specified");
        {
            juce::UndoManager undo;
            motion::Document document {undo};
            auto project = generatorProject("spiral");
            auto& turns = project.tracks.change(0).clips[0].properties["generator.turns"];
            turns.setKey({0, 2, motion::Interpolation::linear});
            turns.setKey({1, 6, motion::Interpolation::linear});
            const auto clipId = project.tracks[0].clips[0].id;
            document.reset(std::move(project));
            motion::Project loaded;
            const auto result = motion::Document::prepareLoad(document.save(), loaded);
            expect(result.wasOk(), result.getErrorMessage());
            const auto& properties = loaded.tracks[0].clips[0].properties;
            expect(properties.contains("generator.turns") && properties.at("generator.turns").keyframes().size() == 2, "the keyed turns come back");
            expect(motion::generatorOf(*loaded.assets.front()) == motion::findGenerator("spiral"), "the asset is still a spiral");
            const auto spec = motion::specFor(loaded, clipId, "generator.turns");
            expect(spec.has_value() && spec->label == "Turns", "a generator's parameter has its own name and range");
            expect(motion::ensurePropertyCurve(loaded, clipId, "generator.radius") != nullptr, "a parameter the clip lacks is made at its default");
            expect(motion::ensurePropertyCurve(loaded, clipId, "generator.points") == nullptr, "another generator's parameter is not");
        }

        beginTest("A generated solid hides what is behind it");
        {
            // A box in front of a star: the star's middle is hidden.
            auto project = generatorProject("star");
            auto box = generatorAsset(5, "box");
            project.assets.push_back(box);
            auto clip = motion::Document::makeClip(6, *box, 0);
            clip.duration = 2;
            clip.properties["position.z"] = motion::Curve(1);
            clip.properties["generator.width"] = motion::Curve(.4);
            clip.properties["generator.height"] = motion::Curve(.4);
            clip.properties["solid"] = motion::Curve(100.0);
            motion::Track track;
            track.id = 7;
            track.insert(clip, project.tempo());
            project.tracks.push_back(track);
            motion::Camera camera;
            camera.id = 8;
            project.cameras.push_back(camera);
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, .5, workspace);
            const auto* star = frame->drawing(0);
            const auto* whole = prepared.clips[0].source->drawingAt(0);
            expect(star != nullptr && star->length() < whole->length(), "some of the star is behind the box");
        }

        beginTest("A grid is solid: it hides what is behind it, and draws all its own lines");
        {
            auto project = generatorProject("star");
            auto grid = generatorAsset(5, "grid");
            project.assets.push_back(grid);
            auto clip = motion::Document::makeClip(6, *grid, 0);
            clip.duration = 2;
            clip.properties["position.z"] = motion::Curve(1);
            clip.properties["generator.width"] = motion::Curve(.4);
            clip.properties["generator.height"] = motion::Curve(.4);
            clip.properties["solid"] = motion::Curve(100.0);
            motion::Track track;
            track.id = 7;
            track.insert(clip, project.tempo());
            project.tracks.push_back(track);
            motion::Camera camera;
            camera.id = 8;
            project.cameras.push_back(camera);
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = motion::hidden::cull(prepared, .5, workspace);
            const auto* star = frame->drawing(0);
            const auto* whole = prepared.clips[0].source->drawingAt(0);
            expect(star != nullptr && star->length() < whole->length(), "some of the star is behind the grid");
            expect(frame->drawing(1) == nullptr, "the grid loses none of its lines");
        }
    }

private:
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
    static motion::Project generatorProject(const char* kind) {
        motion::Project project;
        project.duration = 2;
        const auto asset = generatorAsset(1, kind);
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(2, *asset, 0);
        clip.duration = 2;
        motion::Track track;
        track.id = 3;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
        return project;
    }
};

static MotionGeneratorsTest motionGeneratorsTest;
