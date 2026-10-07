#include <JuceHeader.h>
#include "../Source/motion/render/BeamRenderer.h"
#include <thread>
#include "../Source/motion/import/FractalPreparation.h"
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/CompositionRenderer.h"
#include "../Source/motion/model/PropertyTarget.h"
#include "../Source/motion/ui/MotionPath.h"
#include "../Source/motion/export/SoundtrackExporter.h"
#include "../Source/motion/import/VideoSourcePreparer.h"
#include "../Source/motion/export/SignalExporter.h"

class MotionDocumentTest : public juce::UnitTest {
public:
    MotionDocumentTest() : juce::UnitTest("Motion document and signal", "Motion") {}
    void testRippleDelete(const motion::Project& source) {
        beginTest("Ripple deletion closes selected intervals per track and preserves source clocks, keys and cues");
        auto project = source;
        project.tracks.resize(2, project.tracks.front());
        project.tracks.change(0).id = 100; project.tracks.change(1).id = 101;
        const auto base = project.tracks[0].clips.front();
        project.tracks.change(0).clips.clear(); project.tracks.change(1).clips.clear();
        for (int index = 0; index < 5; ++index) {
            auto clip = base;
            clip.id = 200 + index; clip.start = index * 3; clip.duration = 2;
            clip.offset = .75; clip.rate = 1.25;
            clip.properties["position.x"].setKey({1, .5, motion::Interpolation::linear});
            if (index == 4) { expect(clip.anchorToBeats(project.tempo())); }
            project.tracks.change(0).clips.push_back(clip);
        }
        auto independent = base; independent.id = 300; independent.start = 9; independent.duration = 2;
        project.tracks.change(1).clips.push_back(independent);
        juce::UndoManager undo; motion::Document document(undo); document.reset(project);
        expect(document.setMarker(0, 8, "Fixed cue").wasOk());
        const auto original = document.save().toString();
        const auto revision = document.revision();
        expect(document.removeClips({201, 9999}, true).failed());
        expect(document.removeClips({201, 201}, true).failed());
        expect(document.removeClips({}, true).failed());
        expectEquals(document.revision(), revision); expectEquals(document.save().toString(), original);
        expect(document.removeClips({201, 203}, true).wasOk());
        const auto& result = document.project();
        expectEquals(static_cast<int>(result.tracks[0].clips.size()), 3);
        expectEquals(result.tracks[0].clips[1].start, 4.0);
        const auto& last = result.tracks[0].clips[2];
        expectWithinAbsoluteError(last.timing(result.tempo()).start, 8.0, 1e-12);
        expectWithinAbsoluteError(last.timing(result.tempo()).offset, .75, 1e-12);
        expectWithinAbsoluteError(last.timing(result.tempo()).rate, 1.25, 1e-12);
        expect(last.timeBase == motion::ClipTimeBase::beats);
        expectEquals(last.properties.at("position.x").evaluate(1), .5);
        expectEquals(result.tracks[1].clips[0].start, 9.0);
        expectEquals(result.markers[0].time, 8.0);
        expectEquals(result.duration, project.duration);
        const auto rippled = document.save().toString();
        expect(undo.undo()); expectEquals(document.save().toString(), original);
        expect(undo.redo()); expectEquals(document.save().toString(), rippled);
        juce::UndoManager loadedUndo; motion::Document loaded(loadedUndo);
        expect(loaded.load(document.save()).wasOk()); expectEquals(loaded.save().toString(), rippled);
        expect(undo.undo());
        expect(document.removeClips({201, 203}, false).wasOk());
        expectEquals(document.project().tracks[0].clips[1].start, 6.0);
        expect(undo.undo());
        document.edit("Lock second track", [](motion::Project& value) { value.tracks.change(1).locked = true; });
        const auto locked = document.save().toString();
        expect(document.removeClips({201, 300}, true).failed()); expectEquals(document.save().toString(), locked);
        expect(document.removeClips({201}, true).wasOk());

        beginTest("Ripple retains touching mixed-timebase boundaries at non-round tempo");
        project.bpm = 137;
        project.tracks.resize(1); project.tracks.change(0).clips.clear();
        for (int index = 0; index < 5; ++index) {
            auto clip = base; clip.id = 400 + index; clip.start = index * .7; clip.duration = .7;
            if (index % 2 == 1) { expect(clip.anchorToBeats(project.tempo())); }
            project.tracks.change(0).clips.push_back(clip);
        }
        document.reset(project);
        expect(document.removeClips({401, 403}, true).wasOk());
        expectWithinAbsoluteError(document.project().tracks[0].clips.back().timing(motion::Tempo(137)).start, 1.4, 1e-12);
        project.tracks.change(0).clips.clear();
        for (const auto [start, duration] : {std::pair{0.0, .3}, std::pair{.3, .4}, std::pair{.7, .1}}) {
            auto clip = base; clip.id = 500 + project.tracks[0].clips.size(); clip.start = start; clip.duration = duration;
            project.tracks.change(0).clips.push_back(clip);
        }
        document.reset(project);
        expect(document.removeClips({500}, true).wasOk());
        expectEquals(document.project().tracks[0].clips[1].start, document.project().tracks[0].clips[0].end());
    }

    void testMarkers() {
        beginTest("Markers support scoped editing, undo, persistence and independent composition copies");
        juce::UndoManager undo;
        motion::Document document(undo);
        expect(document.setMarker(0, 4.125, " Verse ").wasOk());
        const auto marker = document.project().markers.front();
        expectEquals(marker.name, juce::String("Verse"));
        expect(undo.undo()); expect(document.project().markers.empty());
        expect(undo.redo());
        expect(document.setMarker(marker.id, 6.25, "Chorus").wasOk());
        expect(undo.undo()); expectEquals(document.project().markers.front().time, 4.125);
        expect(document.removeMarker(marker.id).wasOk()); expect(document.project().markers.empty());
        expect(undo.undo());
        const auto saved = document.save().toString();
        expect(document.setMarker(0, 4.125, "Duplicate position").failed());
        expect(document.setMarker(0, -1, "Negative").failed());
        expect(document.setMarker(0, 181, "Outside").failed());
        expect(document.setMarker(0, 0, "\n").failed());
        expectEquals(document.save().toString(), saved);
        juce::UndoManager loadedUndo;
        motion::Document loaded(loadedUndo);
        expect(loaded.load(document.save()).wasOk());
        expectEquals(loaded.project().markers.front().time, 4.125);
        auto invalid = document.save();
        invalid.getChildByName("marker")->setAttribute("time", "nan");
        expect(loaded.load(invalid).failed());
        expectEquals(loaded.save().toString(), saved);

        motion::Project project;
        auto definition = std::make_shared<motion::CompositionDefinition>();
        definition->id = 100; definition->name = "Nested";
        project.definitions.push_back(definition);
        motion::Track track; track.id = 102;
        auto instance = motion::Document::makeCompositionClip(101, *definition, 0);
        expect(track.insert(instance, motion::Tempo(120))); project.tracks.push_back(track);
        document.reset(project);
        expect(document.setMarker(0, 8, "Main cue").wasOk());
        expect(document.enterComposition(100).wasOk());
        expect(document.project().markers.empty());
        expect(document.setMarker(0, 2, "Nested cue").wasOk());
        const auto nested = document.project().markers.front();
        expect(document.enterComposition(0).wasOk());
        expectEquals(document.project().markers.front().name, juce::String("Main cue"));
        motion::Id copied = 0;
        expect(document.makeCompositionUnique(101, copied).wasOk());
        const auto copy = std::find_if(document.project().definitions.begin(), document.project().definitions.end(), [copied](const auto& value) { return value->id == copied; });
        expect(copy != document.project().definitions.end());
        if (copy != document.project().definitions.end()) {
            expect((*copy)->markers.size() == 1);
            expect((*copy)->markers.front().id != nested.id);
            expectEquals((*copy)->markers.front().time, nested.time);
        }
        expect(loaded.load(document.save()).wasOk());
        const auto valid = loaded.save().toString();
        auto duplicateIdentity = document.save();
        duplicateIdentity.getChildByName("marker")->setAttribute("id", "102");
        expect(loaded.load(duplicateIdentity).failed());
        expectEquals(loaded.save().toString(), valid);
    }

    void testFractal() {
        beginTest("Fractal expansion is bounded, normalized and validates branch syntax");
        const juce::String source(R"({"axiom":"F","angle":90,"rules":[{"variable":"F","replacement":"F+F-F-F+F"}]})");
        const auto prepared = motion::fractal::prepare(source, 3);
        expect(static_cast<bool>(prepared));
        expectEquals(static_cast<int>(prepared.segments.size()), 125);
        for (const auto& segment : prepared.segments) {
            for (const auto coordinate : segment) { expect(std::isfinite(coordinate) && std::abs(coordinate) <= 1.000001); }
        }
        expect(!motion::fractal::prepare(juce::String::repeatedString("[", 50000) + "0" + juce::String::repeatedString("]", 50000), 0));
        expect(!motion::fractal::prepare(juce::String(R"({"axiom":"F","extra":'"',"deep":)") + juce::String::repeatedString("[", 50000) + "0" + juce::String::repeatedString("]", 50000) + "}", 0));
        expect(!motion::fractal::prepare(source, 15));
        expect(!motion::fractal::prepare(source, -1));
        expect(!motion::fractal::prepare(R"({"axiom":"F]"})", 0));
        expect(!motion::fractal::prepare(R"({"axiom":"[F"})", 0));
        expect(!motion::fractal::prepare(R"({"axiom":"X"})", 0));
        expect(!motion::fractal::prepare(R"({"axiom":"F","angle":"90"})", 0));
        expect(!motion::fractal::prepare(R"({"axiom":"F","rules":[{"variable":"F","replacement":"F"},{"variable":"F","replacement":"FF"}]})", 1));
        std::atomic<bool> cancelled {true};
        expect(!motion::fractal::prepare(source, 3, &cancelled));

        beginTest("Fractal depth and prepared geometry survive project reopening; invalid depth is atomic");
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1; asset->name = "Curve.lsystem"; asset->extension = ".lsystem"; asset->fractalDepth = 4;
        asset->data.append(source.toRawUTF8(), source.getNumBytesAsUTF8());
        expect(motion::decodeAsset(*asset).wasOk());
        juce::UndoManager undo, restoreUndo;
        motion::Document document(undo), restored(restoreUndo);
        document.edit("Fractal", [&](motion::Project& project) { project.assets.push_back(asset); });
        const auto xml = document.save();
        expect(restored.load(xml).wasOk());
        if (!restored.project().assets.empty()) {
            const auto reopened = restored.project().assets.front();
            expectEquals(reopened->fractalDepth, 4);
            for (int index = 0; index < 1000; ++index) {
                const auto before = asset->source->sample(0, index / 1000.0);
                const auto after = reopened->source->sample(0, index / 1000.0);
                expectWithinAbsoluteError(before.x, after.x, 1.0e-6f);
                expectWithinAbsoluteError(before.y, after.y, 1.0e-6f);
            }
        }
        const auto valid = restored.save().toString();
        auto invalid = xml;
        invalid.getChildByName("asset")->setAttribute("fractalDepth", 16);
        expect(restored.load(invalid).failed());
        expectEquals(restored.save().toString(), valid);
        const auto original = asset->source;
        asset->fractalDepth = 16;
        expect(motion::decodeAsset(*asset).failed());
        expect(asset->source == original);
    }

    void testMotionPath() {
        beginTest("Motion paths map clip-local keys through offsets, rates, beats and parent transforms");
        motion::Project project;
        project.duration = 10;
        project.bpm = 120;
        motion::Group parent;
        parent.id = 10;
        parent.properties["position.x"] = motion::Curve(10);
        parent.properties["position.x"].setKey({3, 20, motion::Interpolation::linear});
        parent.properties["rotation.z"] = motion::Curve(90);
        parent.properties["scale.x"] = motion::Curve(2);
        project.groups.push_back(parent);

        motion::Clip clip;
        clip.id = 20;
        clip.start = 2;
        clip.duration = 4;
        clip.offset = 1;
        clip.rate = 2;
        clip.properties["position.x"] = motion::Curve(0);
        clip.properties["position.x"].setKey({0, -4, motion::Interpolation::linear});
        clip.properties["position.x"].setKey({1, 0, motion::Interpolation::hold});
        clip.properties["position.x"].setKey({3, 1, motion::Interpolation::linear});
        clip.properties["position.x"].setKey({10, 8, motion::Interpolation::linear});
        expect(clip.anchorToBeats(project.tempo()));
        motion::Track track;
        track.id = 30;
        track.group = parent.id;
        track.clips.push_back(clip);
        project.tracks.push_back(track);

        const auto path = motion::editor::buildMotionPath(project, clip.id);
        expect(!path.tooComplex);
        expect(!path.points.empty());
        const auto keyAtThree = std::find_if(path.points.begin(), path.points.end(), [](const auto& point) {
            return point.key && point.time == 3.0;
        });
        expect(keyAtThree != path.points.end());
        if (keyAtThree != path.points.end()) {
            expect(keyAtThree->key);
            expect(keyAtThree->dot);
            expect(keyAtThree->breakBefore);
            expectWithinAbsoluteError(keyAtThree->position.x, 20.0, 1.0e-12);
            expectWithinAbsoluteError(keyAtThree->position.y, 2.0, 1.0e-12);
            expectWithinAbsoluteError(keyAtThree->position.z, 0.0, 1.0e-12);
        }
        for (const auto& point : path.points) {
            expect(point.time >= 2.0 && point.time <= 6.0);
            expect(point.position.finite());
        }
        expect(std::none_of(path.points.begin(), path.points.end(), [](const auto& point) { return point.key && point.time < 2.0; }));
        expect(std::none_of(path.points.begin(), path.points.end(), [](const auto& point) { return point.key && point.time > 6.0; }));

        beginTest("Motion paths reject non-finite transformed positions and bound excessive authored keys");
        auto nonFinite = project;
        nonFinite.tracks.change(0).clips[0].properties["position.x"] = motion::Curve(2);
        nonFinite.groups[0].properties["scale.x"] = motion::Curve(std::numeric_limits<double>::max());
        const auto finitePath = motion::editor::buildMotionPath(nonFinite, clip.id);
        expect(finitePath.points.empty());
        expect(!finitePath.tooComplex);

        auto excessive = project;
        auto& curve = excessive.tracks.change(0).clips[0].properties["position.x"];
        for (int index = 0; index < 2049; ++index) {
            curve.setKey({20.0 + index, static_cast<double>(index), motion::Interpolation::linear});
        }
        const auto complexPath = motion::editor::buildMotionPath(excessive, clip.id);
        expect(complexPath.tooComplex);
        expect(complexPath.points.empty());
    }

    void runTest() override {
        testFractal();
        testMotionPath();
        testMarkers();
        beginTest("Plain titles preserve natural proportions and explicit newlines");
        const auto textAspect = [this](const juce::String& text, motion::TextSettings settings = {}) {
            motion::Asset title;
            title.extension = ".txt";
            title.textSettings = settings;
            title.data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
            const auto result = motion::decodeAsset(title);
            expect(result.wasOk(), result.getErrorMessage());
            if (result.failed() || title.source == nullptr) { return 0.0f; }
            float minX = 100, maxX = -100, minY = 100, maxY = -100;
            for (int i = 0; i < 10000; ++i) {
                const auto point = title.source->sample(0, i / 10000.0);
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }
            return (maxX - minX) / std::max(0.0001f, maxY - minY);
        };
        const auto singleLine = textAspect("PHASE / SPACE");
        const auto explicitLines = textAspect("PHASE /\nSPACE");
        expect(singleLine > 8.0f, "A title should not wrap to fit a square");
        expect(explicitLines > 1.0f && explicitLines < singleLine * 0.5f, "Explicit line breaks must remain effective");
        beginTest("Typography changes prepared geometry and validates numeric limits");
        motion::TextSettings typography;
        typography.tracking = 0.3;
        expect(textAspect("PHASE / SPACE", typography) > singleLine * 1.2f);
        typography.tracking = 0;
        typography.lineSpacing = 2.4;
        expect(textAspect("PHASE /\nSPACE", typography) < explicitLines * 0.8f);
        typography.lineSpacing = std::numeric_limits<double>::quiet_NaN();
        expect(typography.validate().isNotEmpty());
        typography = {};
        typography.style = 4;
        expect(typography.validate().isNotEmpty());

        beginTest("Typography is persisted and invalid styles reject atomically");
        juce::UndoManager typographyUndo;
        motion::Document typographyDocument(typographyUndo);
        auto title = std::make_shared<motion::Asset>();
        title->id = 1; title->name = "Title.txt"; title->extension = ".txt";
        const juce::String titleText("WIDE TITLE\nI");
        title->data.append(titleText.toRawUTF8(), titleText.getNumBytesAsUTF8());
        title->textSettings.family = juce::Font::getDefaultMonospacedFontName();
        title->textSettings.style = juce::Font::bold;
        title->textSettings.alignment = 1;
        title->textSettings.lineSpacing = 1.8;
        title->textSettings.tracking = 0.15;
        expect(motion::decodeAsset(*title).wasOk());
        typographyDocument.edit("Title", [&](motion::Project& project) { project.assets.push_back(title); });
        const auto typographyXml = typographyDocument.save();
        juce::UndoManager typographyRestoreUndo;
        motion::Document typographyRestored(typographyRestoreUndo);
        expect(typographyRestored.load(typographyXml).wasOk());
        expect(typographyRestored.project().assets.front()->textSettings == title->textSettings);
        for (int sample = 0; sample < 1000; ++sample) {
            const auto phase = sample / 1000.0;
            const auto original = title->source->sample(0, phase);
            const auto reopened = typographyRestored.project().assets.front()->source->sample(0, phase);
            expectWithinAbsoluteError(original.x, reopened.x, 1.0e-6f);
            expectWithinAbsoluteError(original.y, reopened.y, 1.0e-6f);
        }

        const auto savedTypography = typographyRestored.save().toString();
        auto badTypography = typographyXml;
        badTypography.getChildByName("asset")->getChildByName("typography")->setAttribute("tracking", "nan");
        expect(typographyRestored.load(badTypography).failed());
        expectEquals(typographyRestored.save().toString(), savedTypography);

        motion::Asset oversizedTitle;
        oversizedTitle.extension = ".txt";
        const auto oversizedText = juce::String::repeatedString("A", 16385);
        oversizedTitle.data.append(oversizedText.toRawUTF8(), oversizedText.getNumBytesAsUTF8());
        expect(motion::decodeAsset(oversizedTitle).failed());
        expect(oversizedTitle.source == nullptr);

        juce::UndoManager undo;
        motion::Document document(undo);
        auto asset = std::make_shared<motion::Asset>();
        asset->id = document.newId();
        asset->name = "triangle.obj";
        asset->extension = ".obj";
        const juce::String source("v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n");
        asset->data.append(source.toRawUTF8(), source.getNumBytesAsUTF8());
        beginTest("Imported assets and independent clip curves survive a project round trip");
        const auto decoded = motion::decodeAsset(*asset);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        if (decoded.failed()) {
            return;
        }
        auto clip = motion::Document::makeClip(document.newId(), *asset, 0);
        clip.duration = 180;
        clip.properties["position.x"].setKey({0, 0, motion::Interpolation::linear});
        clip.properties["position.x"].setKey({180, 1});
        motion::Track track;
        track.id = document.newId();
        track.name = "Hero";
        expect(track.insert(clip, motion::Tempo(120)));
        document.edit("Import", [&](motion::Project& project) {
            project.assets.push_back(asset);
            project.tracks.push_back(track);
        });
        const auto xml = document.save();
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto loaded = restored.load(xml);
        expect(loaded.wasOk(), loaded.getErrorMessage());
        expectEquals(static_cast<int>(restored.project().assets.size()), 1);
        expectEquals(static_cast<int>(restored.project().tracks.size()), 1);
        expectWithinAbsoluteError(restored.project().tracks[0].clips[0].properties.at("position.x").evaluate(90), 0.5, 1.0e-9);
        expect(restored.newId() > track.id);

        beginTest("Background load preparation does not publish and failures retain the destination");
        motion::Project preparedLoad;
        preparedLoad.name = "Untouched destination";
        const auto beforePreparation = document.save().toString();
        const auto preparedGeneration = document.generation();
        std::atomic<bool> cancelled {true};
        expect(motion::Document::prepareLoad(xml, preparedLoad, &cancelled).failed());
        expectEquals(preparedLoad.name, juce::String("Untouched destination"));
        cancelled.store(false);
        auto invalidPreparation = xml;
        invalidPreparation.getChildByName("asset")->setAttribute("id", "0");
        expect(motion::Document::prepareLoad(invalidPreparation, preparedLoad, &cancelled).failed());
        expectEquals(preparedLoad.name, juce::String("Untouched destination"));
        auto preparationResult = juce::Result::fail("Not run");
        std::thread preparationThread([&] { preparationResult = motion::Document::prepareLoad(xml, preparedLoad, &cancelled); });
        preparationThread.join();
        expect(preparationResult.wasOk(), preparationResult.getErrorMessage());
        expect(document.generation() == preparedGeneration);
        expectEquals(document.save().toString(), beforePreparation);
        expectEquals(static_cast<int>(preparedLoad.assets.size()), 1);
        expectEquals(static_cast<int>(preparedLoad.tracks.size()), 1);
        expect(preparedLoad.assets.front()->source != nullptr);

        beginTest("Undo and redo retain shared asset payloads");
        const auto revisionBeforeUndo = document.revision();
        const auto generationBeforeUndo = document.generation();
        expect(undo.undo());
        expect(document.revision() > revisionBeforeUndo);
        expect(document.generation() == generationBeforeUndo);
        expect(document.project().tracks.empty());
        const auto revisionBeforeRedo = document.revision();
        expect(undo.redo());
        expect(document.revision() > revisionBeforeRedo);
        expect(document.project().assets[0] == asset);

        beginTest("RGB is carried by the sampled signal and a single-object budget fade remains effective");
        auto project = document.project();
        auto& properties = project.tracks.change(0).clips[0].properties;
        properties["red"] = motion::Curve(0.8);
        properties["green"] = motion::Curve(0.2);
        properties["blue"] = motion::Curve(0.4);
        properties["weight"] = motion::Curve(0.25);
        motion::PreparedComposition prepared(project);
        const auto lit = prepared.sample(0, 0.1);
        const auto dark = prepared.sample(0, 0.5);
        expectWithinAbsoluteError(lit.r, 0.8f, 0.00001f);
        expectWithinAbsoluteError(lit.g, 0.2f, 0.00001f);
        expectWithinAbsoluteError(lit.b, 0.4f, 0.00001f);
        expectEquals(dark.r + dark.g + dark.b, 0.0f);
        int drawnSamples = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto point = prepared.sample(0, i / 1000.0);
            drawnSamples += point.r > 0 ? 1 : 0;
        }
        expectEquals(drawnSamples, 250);

        beginTest("Extreme transforms cannot send non-finite coordinates to the output");
        properties["scale.x"] = motion::Curve(std::numeric_limits<double>::max());
        motion::PreparedComposition extreme(project);
        for (int i = 0; i < 100; ++i) {
            const auto point = extreme.sample(0, i / 100.0);
            expect(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
            expect(std::isfinite(point.r) && std::isfinite(point.g) && std::isfinite(point.b));
        }

        beginTest("Malformed projects are rejected atomically");
        auto invalid = std::make_unique<juce::XmlElement>(xml);
        invalid->setAttribute("duration", -1);
        expect(restored.load(*invalid).failed());
        expectEquals(static_cast<int>(restored.project().tracks.size()), 1);
        expectEquals(restored.project().duration, 180.0);
        auto duplicate = std::make_unique<juce::XmlElement>(xml);
        duplicate->addChildElement(new juce::XmlElement(*duplicate->getChildByName("asset")));
        expect(restored.load(*duplicate).failed());
        expectEquals(static_cast<int>(restored.project().assets.size()), 1);
        testCameras(document);
        testAnimatedSources();
        testEffects(document.project());
        testCompositions(document.project());
        testCompositionCreation(document.project());
        testRoundTripKeyEditing(document.project());
        testTrackStates(document.project());
        testGroups(document.project());
        testModulation(document.project());
        testAudioImports(document.project());
        testAudioExports(document.project());
        testTiming(document.project());
        testScopeProfile(document.project());
        testRippleDelete(document.project());
    }

private:
    void testScopeProfile(const motion::Project& source) {
        beginTest("Scope timing round trips exactly, edits from nested compositions and rejects invalid values");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(source);
        expect(document.project().scope == motion::ScopeProfile{});
        const motion::ScopeProfile awkward {1.0 / 3, 0.1 + 0.2, 123.456789012345};
        document.edit("Scope", [awkward](motion::Project& project) { project.scope = awkward; });
        const auto xml = document.save();
        juce::UndoManager loadedUndo;
        motion::Document loaded(loadedUndo);
        expect(loaded.load(xml).wasOk());
        expect(loaded.project().scope == awkward, "every scope time reloads bit-exactly");
        expect(motion::PreparedComposition(loaded.project()).scope == awkward, "prepared compositions carry the profile");
        expect(undo.undo());
        expect(document.project().scope == motion::ScopeProfile{});
        expect(undo.redo());
        expect(document.project().scope == awkward);

        auto legacy = xml;
        for (const auto* attribute : {"scopeDwell", "scopeTravel", "scopeSettle"}) { legacy.removeAttribute(attribute); }
        expect(loaded.load(legacy).wasOk());
        expect(loaded.project().scope == motion::ScopeProfile{}, "projects without a profile use the analog scope defaults");
        expect(loaded.load(xml).wasOk());
        const auto unchanged = loaded.save().toString();
        for (const auto* attribute : {"scopeDwell", "scopeTravel", "scopeSettle"}) {
            for (const auto* value : {"-1", "nan", "inf", "1e9"}) {
                auto invalid = xml;
                invalid.setAttribute(attribute, value);
                expect(loaded.load(invalid).failed(), juce::String(attribute) + "=" + value);
                expectEquals(loaded.save().toString(), unchanged);
            }
        }

        motion::Project nested;
        auto definition = std::make_shared<motion::CompositionDefinition>();
        definition->id = 100; definition->name = "Nested";
        nested.definitions.push_back(definition);
        motion::Track track; track.id = 102;
        expect(track.insert(motion::Document::makeCompositionClip(101, *definition, 0), motion::Tempo(120)));
        nested.tracks.push_back(track);
        document.reset(nested);
        expect(document.enterComposition(100).wasOk());
        const motion::ScopeProfile laser = motion::scopeProfilePresets[1].profile;
        document.edit("Scope", [laser](motion::Project& project) { project.scope = laser; });
        expect(document.project().scope == laser);
        expect(document.enterComposition(0).wasOk());
        expect(document.mainProject().scope == laser, "the profile belongs to the project, whichever composition is open");
        expect(undo.undo());
        expect(document.mainProject().scope == motion::ScopeProfile{});
    }

    void testTiming(const motion::Project& source) {
        beginTest("Musical grid settings round trip without retiming object clips or keys");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(source);
        const auto start = document.project().tracks[0].clips[0].start;
        const auto keyTime = document.project().tracks[0].clips[0].properties.at("position.x").keyframes().back().time;
        document.edit("Timing", [](motion::Project& project) {
            project.timeDisplay = motion::TimeDisplay::beats;
            project.bpm = 90;
            project.beatsPerBar = 3;
            project.snapBeats = 1.0 / 3;
            project.gridSnap = false;
            project.frameRate = 24;
        });
        expectEquals(document.project().tracks[0].clips[0].start, start);
        expectEquals(document.project().tracks[0].clips[0].properties.at("position.x").keyframes().back().time, keyTime);
        const auto xml = document.save();
        juce::UndoManager loadedUndo;
        motion::Document loaded(loadedUndo);
        expect(loaded.load(xml).wasOk());
        expect(loaded.project().timeDisplay == motion::TimeDisplay::beats);
        expectEquals(loaded.project().beatsPerBar, 3);
        expectEquals(loaded.project().bpm, 90.0);
        expectEquals(loaded.project().frameRate, 24.0);
        expectWithinAbsoluteError(loaded.project().snapBeats, 1.0 / 3, 1.0e-12);
        expect(!loaded.project().gridSnap);
        expect(undo.undo());
        expectEquals(document.project().beatsPerBar, source.beatsPerBar);
        expect(document.project().timeDisplay == source.timeDisplay, "undoing the step that changed the display reverts it");
        expect(undo.redo());
        expectEquals(document.project().beatsPerBar, 3);
        beginTest("Invalid timing settings reject atomically");
        const auto unchanged = loaded.save().toString();
        for (const auto* attribute : { "timeDisplay", "beatsPerBar", "snapBeats", "gridSnap", "bpm", "fps" }) {
            auto invalid = xml;
            invalid.setAttribute(attribute, -1);
            expect(loaded.load(invalid).failed());
            expectEquals(loaded.save().toString(), unchanged);
        }
    }

    void testAudioExports(const motion::Project& sourceProject) {
        beginTest("Soundtrack WAV export writes exact stereo clock, gain and pan without visual signal");
        juce::TemporaryFile directory(".motion-soundtrack-export-tests");
        const auto created = directory.getFile().createDirectory();
        expect(created.wasOk(), created.getErrorMessage());
        if (created.failed()) {
            return;
        }
        auto asset = std::make_shared<motion::Asset>(wavAsset(2));
        const auto decoded = motion::decodeAsset(*asset);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        if (decoded.failed()) {
            return;
        }
        constexpr double rate = 16000;
        auto project = sourceProject;
        project.duration = 10.6 / rate;
        project.assets.push_back(asset);
        motion::Track track;
        track.id = 9901;
        track.kind = motion::TrackKind::audio;
        auto clip = motion::Document::makeClip(9902, *asset, 2 / rate);
        clip.properties["gain"] = motion::Curve(0.5);
        clip.properties["pan"] = motion::Curve(0.5);
        track.clips.push_back(clip);
        project.tracks.push_back(track);
        std::atomic<bool> cancel { false };
        std::atomic<double> progress { -1 };
        const auto output = directory.getFile().getChildFile("soundtrack.wav");
        const auto exported = motion::SoundtrackExporter::write(project, output, rate, cancel, &progress);
        expect(exported.wasOk(), exported.getErrorMessage());
        if (exported.failed()) {
            return;
        }
        expectEquals(progress.load(), 1.0);
        const auto read = [](const juce::File& file) {
            juce::WavAudioFormat format;
            auto input = file.createInputStream();
            return std::unique_ptr<juce::AudioFormatReader>(input == nullptr ? nullptr : format.createReaderFor(input.release(), true));
        };
        auto reader = read(output);
        expect(reader != nullptr);
        if (reader == nullptr) {
            return;
        }
        expectEquals(static_cast<int>(reader->numChannels), 2);
        expectEquals(static_cast<int>(reader->bitsPerSample), 32);
        expect(reader->usesFloatingPointData);
        expectEquals(reader->sampleRate, rate);
        expectEquals(reader->lengthInSamples, static_cast<juce::int64>(11));
        juce::AudioBuffer<float> samples(2, 11);
        expect(reader->read(samples.getArrayOfWritePointers(), 2, 0, 11));
        motion::PreparedComposition prepared(project);
        for (int index = 0; index < 11; ++index) {
            const auto expected = prepared.soundtrack.sample(index / rate);
            expectWithinAbsoluteError(samples.getSample(0, index), expected.left, 0.000001f);
            expectWithinAbsoluteError(samples.getSample(1, index), expected.right, 0.000001f);
        }
        expectWithinAbsoluteError(samples.getSample(0, 4), 0.125f, 0.000001f);
        expectWithinAbsoluteError(samples.getSample(1, 4), -0.125f, 0.000001f);
        expectEquals(samples.getSample(0, 0), 0.0f);
        expectEquals(samples.getSample(0, 10), 0.0f);
        reader.reset();
        juce::MemoryBlock original;
        expect(output.loadFileAsData(original));
        const auto repeatedFile = directory.getFile().getChildFile("repeated.wav");
        expect(motion::SoundtrackExporter::write(prepared, repeatedFile, rate, cancel).wasOk());
        juce::MemoryBlock repeated;
        expect(repeatedFile.loadFileAsData(repeated));
        expect(original == repeated);

        beginTest("Projects without audio tracks export stereo silence for the whole duration");
        project.tracks.erase(project.tracks.end() - 1);
        const auto silentFile = directory.getFile().getChildFile("silent.wav");
        expect(motion::SoundtrackExporter::write(project, silentFile, rate, cancel).wasOk());
        reader = read(silentFile);
        expect(reader != nullptr);
        if (reader != nullptr) {
            samples.clear();
            expect(reader->read(samples.getArrayOfWritePointers(), 2, 0, 11));
            expectEquals(samples.getMagnitude(0, 11), 0.0f);
        }
        reader.reset();

        beginTest("Cancellation and invalid samples preserve an existing destination WAV");
        cancel.store(true);
        expect(motion::SoundtrackExporter::write(prepared, output, rate, cancel, &progress).failed());
        expectEquals(progress.load(), 0.0);
        cancel.store(false);
        const auto midCancel = motion::WavExporter::write<2>(4096 / rate, output, rate, cancel, &progress,
            [&](double index, double) {
                if (index == 8) {
                    cancel.store(true);
                }
                return std::array<float, 2> { 0.5f, -0.5f };
            }, "Soundtrack", "Non-finite soundtrack sample.");
        expect(midCancel.failed());
        cancel.store(false);
        expect(motion::WavExporter::write<2>(11 / rate, output, rate, cancel, nullptr,
            [](double, double) { return std::array<float, 2> { std::numeric_limits<float>::quiet_NaN(), 0 }; },
            "Soundtrack", "Non-finite soundtrack sample.").failed());
        expect(motion::SoundtrackExporter::write(prepared, output, -1, cancel).failed());
        juce::MemoryBlock unchanged;
        expect(output.loadFileAsData(unchanged));
        expect(unchanged == original);
    }

    static motion::Asset wavAsset(int channels) {
        juce::MemoryOutputStream wav;
        const int frameCount = 4;
        const int dataBytes = frameCount * channels * 2;
        wav.write("RIFF", 4);
        wav.writeInt(36 + dataBytes);
        wav.write("WAVEfmt ", 8);
        wav.writeInt(16);
        wav.writeShort(1);
        wav.writeShort(static_cast<short>(channels));
        wav.writeInt(8000);
        wav.writeInt(8000 * channels * 2);
        wav.writeShort(static_cast<short>(channels * 2));
        wav.writeShort(16);
        wav.write("data", 4);
        wav.writeInt(dataBytes);
        const std::array<short, 4> samples { 0, 16384, -16384, 32767 };
        for (int frame = 0; frame < frameCount; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                wav.writeShort(channel == 0 ? samples[frame] : static_cast<short>(-samples[frame] / 2));
            }
        }
        motion::Asset asset;
        asset.id = 9000;
        asset.name = "Soundtrack.wav";
        asset.extension = ".wav";
        asset.data = wav.getMemoryBlock();
        return asset;
    }

    void testAudioImports(const motion::Project& sourceProject) {
        beginTest("Audio assets decode embedded PCM with stereo and mono boundary semantics");
        auto audio = std::make_shared<motion::Asset>(wavAsset(2));
        std::atomic<double> progress { -1 };
        const auto result = motion::decodeAsset(*audio, nullptr, &progress);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) {
            return;
        }
        expectEquals(progress.load(), 1.0);
        expect(audio->source == nullptr);
        expectEquals(static_cast<int>(audio->audio->channelCount()), 2);
        expectEquals(static_cast<int>(audio->audio->frameCount()), 4);
        expectWithinAbsoluteError(audio->audio->sample(1.0 / 8000).left, 0.5f, 0.00001f);
        expectWithinAbsoluteError(audio->audio->sample(1.0 / 8000).right, -0.25f, 0.00001f);
        expectWithinAbsoluteError(audio->audio->sample(0.5 / 8000).left, 0.25f, 0.00001f);
        expectEquals(audio->audio->sample(-1).left, 0.0f);
        expectEquals(audio->audio->sample(audio->audio->duration()).right, 0.0f);
        auto mono = wavAsset(1);
        const auto monoResult = motion::decodeAsset(mono);
        expect(monoResult.wasOk(), monoResult.getErrorMessage());
        if (monoResult.wasOk()) {
            expectEquals(mono.audio->sample(1.0 / 8000).left, mono.audio->sample(1.0 / 8000).right);
        }
        auto multichannel = wavAsset(3);
        expect(motion::decodeAsset(multichannel).failed());
        expect(multichannel.audio == nullptr);
        auto malformed = textAsset(".wav", "not audio");
        expect(motion::decodeAsset(malformed).failed());
        const auto retained = audio->audio;
        std::atomic<bool> cancelled { true };
        expect(motion::decodeAsset(*audio, &cancelled, &progress).failed());
        expect(audio->audio == retained);
        expectEquals(progress.load(), 0.0);

        beginTest("Soundtrack clips retain source duration, timing, gain and pan in project state");
        auto project = sourceProject;
        project.assets.push_back(audio);
        auto clip = motion::Document::makeClip(9002, *audio, 1);
        expectEquals(clip.duration, audio->audio->duration());
        expectEquals(static_cast<int>(clip.properties.size()), 2);
        expectEquals(clip.properties.at("gain").base, 1.0);
        expectEquals(clip.properties.at("pan").base, 0.0);
        clip.properties["gain"].setKey({ 0, 0.5, motion::Interpolation::linear });
        clip.properties["gain"].setKey({ clip.duration, 1 });
        clip.properties["pan"] = motion::Curve(-0.25);
        clip.offset = 0.0001;
        clip.rate = 1.5;
        motion::Track track;
        track.id = 9001;
        track.name = "Soundtrack";
        track.kind = motion::TrackKind::audio;
        expect(track.insert(clip, motion::Tempo(120)));
        project.tracks.push_back(track);
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(sourceProject);
        document.edit("Import soundtrack", [&](motion::Project& value) { value = project; });
        expect(undo.undo());
        expectEquals(static_cast<int>(document.project().tracks.size()), static_cast<int>(sourceProject.tracks.size()));
        expect(undo.redo());
        expect(document.project().assets.back() == audio);
        juce::UndoManager loadedUndo;
        motion::Document loaded(loadedUndo);
        const auto xml = document.save();
        const auto restored = loaded.load(xml);
        expect(restored.wasOk(), restored.getErrorMessage());
        if (restored.failed()) {
            return;
        }
        const auto& restoredTrack = loaded.project().tracks.back();
        expect(restoredTrack.kind == motion::TrackKind::audio);
        expectWithinAbsoluteError(restoredTrack.clips[0].offset, clip.offset, 1.0e-12);
        expectEquals(restoredTrack.clips[0].rate, clip.rate);
        expectEquals(restoredTrack.clips[0].properties.at("pan").base, -0.25);
        expect(loaded.project().assets.back()->data == audio->data);
        expectWithinAbsoluteError(loaded.project().assets.back()->audio->sample(1.0 / 8000).left, 0.5f, 0.00001f);
        expect(loaded.newId() > clip.id);
        const auto unchanged = loaded.save().toString();
        const auto reject = [&](juce::XmlElement invalid) {
            expect(loaded.load(invalid).failed());
            expectEquals(loaded.save().toString(), unchanged);
        };
        auto wrongVisualKind = xml;
        wrongVisualKind.getChildByName("track")->setAttribute("kind", "audio");
        reject(wrongVisualKind);
        auto wrongAudioKind = xml;
        for (auto* row : wrongAudioKind.getChildWithTagNameIterator("track")) {
            if (row->getStringAttribute("kind") == "audio") {
                row->setAttribute("kind", "visual");
            }
        }
        reject(wrongAudioKind);
        auto unknownKind = xml;
        unknownKind.getChildByName("track")->setAttribute("kind", "unknown");
        reject(unknownKind);
        auto badGain = xml;
        for (auto* row : badGain.getChildWithTagNameIterator("track")) {
            if (row->getStringAttribute("kind") == "audio") {
                row->getChildByName("clip")->getChildByName("property")->setAttribute("base", 5);
            }
        }
        reject(badGain);
        document.edit("Invalid audio effect", [](motion::Project& value) {
            value.tracks.change(value.tracks.size() - 1).effects.push_back(motion::makeEffect(9010, *motion::effectDefinition("bulge")));
        });
        reject(document.save());
        document.edit("Invalid clip effect", [](motion::Project& value) {
            value.tracks.change(value.tracks.size() - 1).effects.clear();
            value.tracks.change(value.tracks.size() - 1).clips[0].effects.push_back(motion::makeEffect(9011, *motion::effectDefinition("bulge")));
        });
        reject(document.save());
    }

    void testModulation(const motion::Project& sourceProject) {
        beginTest("A shared modulator drives every kind of owner on the composition clock");
        auto project = sourceProject;
        project.bpm = 60;
        auto& clip = project.tracks.change(0).clips[0];
        clip.start = 2;
        clip.duration = 10;
        clip.offset = 0.125;
        clip.rate = 2;
        clip.properties["position.x"] = motion::Curve(0);
        motion::Modulator lfo;
        lfo.id = 910;
        lfo.shape.tempoSync = true;
        project.modulators.push_back(lfo);
        motion::Id routeId = 920;
        const auto route = [&](motion::Id target, const std::string& property, double amount = 0.1) {
            motion::ModulationRoute value;
            value.id = routeId++; value.modulator = lfo.id; value.target = target; value.property = property; value.amount = amount;
            project.routes.push_back(value);
        };
        auto clipEffect = motion::makeEffect(900, *motion::effectDefinition("translate"));
        clipEffect.properties["translateX"] = motion::Curve(0);
        clip.effects.push_back(clipEffect);
        auto trackEffect = clipEffect;
        trackEffect.id = 901;
        project.tracks.change(0).effects.push_back(trackEffect);
        motion::Group group;
        group.id = 902;
        auto groupEffect = clipEffect;
        groupEffect.id = 903;
        group.effects.push_back(groupEffect);
        project.groups.push_back(group);
        project.tracks.change(0).group = group.id;
        auto compositionEffect = clipEffect;
        compositionEffect.id = 904;
        project.effects.push_back(compositionEffect);
        motion::Camera camera;
        camera.id = 905;
        project.cameras.push_back(camera);
        route(clip.id, "position.x");
        for (const auto effect : {900, 901, 903, 904}) { route(static_cast<motion::Id>(effect), "translateX"); }
        route(group.id, "position.x");
        route(camera.id, "position.x");
        const double time = 2.25;
        const double localTime = clip.localTime(time, motion::Tempo(120));
        // One beat per cycle at 60 BPM, on the composition clock however the clip is slipped.
        const auto movement = 0.1 * std::sin(time * 2 * std::numbers::pi);
        motion::PreparedComposition prepared(project);
        const auto raw = sourceProject.assets[0]->source->sample(localTime, 0.2);
        const auto world = prepared.clips[0].sample(time, 0.2);
        expectWithinAbsoluteError(world.x, static_cast<float>(raw.x + 5 * movement), 0.00001f);
        // Equal movement of the composition's translation and the camera cancels.
        expectWithinAbsoluteError(prepared.projectPoint(world, time).x, world.x, 0.00001f);
        expectWithinAbsoluteError(prepared.applyCompositionEffects(world, time).x, static_cast<float>(world.x + movement), 0.00001f);
        const auto original = prepared.sample(time, 0.2);
        for (int index = 20; index >= 0; --index) {
            prepared.sample(index * 0.2, 0.2);
        }
        expectEquals(prepared.sample(time, 0.2).x, original.x);

        beginTest("Modulated drawing weight, colour and effect parameters remain bounded");
        auto bounded = project;
        auto& boundedClip = bounded.tracks.change(0).clips[0];
        boundedClip.properties["weight"] = motion::Curve(0.5);
        boundedClip.properties["red"] = motion::Curve(0.5);
        for (const auto* property : {"weight", "red"}) {
            motion::ModulationRoute value;
            value.id = routeId++; value.modulator = lfo.id; value.target = boundedClip.id; value.property = property; value.amount = 1000000;
            bounded.routes.push_back(value);
        }
        const motion::PreparedComposition boundedPrepared(bounded);
        // At 2.75 s the oscillator is at its trough, at 2.25 s its peak.
        expectEquals(boundedPrepared.clips[0].weight(2.75), 0.0);
        expectEquals(boundedPrepared.clips[0].sample(2.75, 0.2).r, 0.0f);
        expect(boundedPrepared.clips[0].weight(time) <= 1000000);
        expectEquals(boundedPrepared.clips[0].sample(time, 0.2).r, 1.0f);
    }

    void testGroups(const motion::Project& sourceProject) {
        beginTest("Nested groups apply inner-to-outer in project time after track effects");
        auto project = sourceProject;
        auto& clip = project.tracks.change(0).clips[0];
        clip.start = 2;
        clip.duration = 10;
        clip.offset = 4;
        clip.rate = 2;
        clip.properties["position.x"] = motion::Curve(0);
        clip.properties["red"] = motion::Curve(1);
        motion::Group outer;
        outer.id = 700;
        outer.name = "Outer";
        outer.properties["scale.x"] = motion::Curve(2);
        outer.properties["red"] = motion::Curve(0.5);
        outer.properties["weight"] = motion::Curve(0.5);
        motion::Group inner;
        inner.id = 701;
        inner.parent = outer.id;
        inner.properties["position.x"].setKey({ 0, 0, motion::Interpolation::linear });
        inner.properties["position.x"].setKey({ 10, 1 });
        inner.properties["red"] = motion::Curve(0.5);
        inner.properties["weight"] = motion::Curve(0.5);
        auto innerEffect = motion::makeEffect(702, *motion::effectDefinition("translate"));
        innerEffect.properties["translateX"] = motion::Curve(0.1);
        inner.effects.push_back(innerEffect);
        auto trackEffect = motion::makeEffect(703, *motion::effectDefinition("translate"));
        trackEffect.properties["translateX"] = motion::Curve(0.2);
        project.tracks.change(0).effects.push_back(trackEffect);
        project.groups = { outer, inner };
        project.tracks.change(0).group = inner.id;
        expect(motion::validGroupHierarchy(project));
        motion::PreparedComposition nested(project);
        const auto raw = sourceProject.assets[0]->source->sample(clip.localTime(3, motion::Tempo(120)), 0.2);
        const auto world = nested.clips[0].sample(3, 0.2);
        expectWithinAbsoluteError(world.x, (raw.x + 0.2f + 0.3f + 0.1f) * 2, 0.00001f);
        expectWithinAbsoluteError(world.r, 0.25f, 0.000001f);
        expectEquals(nested.clips[0].weight(3), 0.25);
        auto amplified = project;
        amplified.tracks.change(0).clips[0].properties["weight"] = motion::Curve(1000000);
        amplified.groups[1].properties["weight"] = motion::Curve(2);
        amplified.groups[0].properties["weight"] = motion::Curve(0.5);
        expectEquals(motion::PreparedComposition(amplified).clips[0].weight(3), 1000000.0);
        amplified.tracks.change(0).clips[0].properties["weight"] = motion::Curve(1);
        amplified.groups[1].properties["weight"] = motion::Curve(1000000);
        amplified.groups[0].properties["weight"] = motion::Curve(1000000);
        motion::Group attenuator;
        attenuator.id = 706;
        attenuator.properties["weight"] = motion::Curve(0.000001);
        amplified.groups[0].parent = attenuator.id;
        amplified.groups.push_back(attenuator);
        expectWithinAbsoluteError(motion::PreparedComposition(amplified).clips[0].weight(3), 1000000.0, 0.000001);

        int lit = 0;
        for (int index = 0; index < 1000; ++index) {
            lit += nested.sample(3, index / 1000.0).r > 0 ? 1 : 0;
        }
        expectEquals(lit, 250);
        const auto target = motion::findPropertyTarget(project, inner.id);
        expect(target.has_value() && target->isGroup && !target->isEffect && !target->camera);
        if (target.has_value()) {
            expectEquals(target->localTime(3), 3.0);
        }
        const auto effectTarget = motion::findPropertyTarget(project, innerEffect.id);
        expect(effectTarget.has_value() && effectTarget->isEffect && effectTarget->localTime(3) == 3);
        expect(motion::findEffectOwner(project, inner.id) == &project.groups[1].effects);
        expect(motion::findEffect(project, innerEffect.id) == &project.groups[1].effects[0]);

        beginTest("Solo groups include descendants and ancestor mute wins over every solo");
        auto outsider = project.tracks[0];
        outsider.id = 704;
        outsider.clips[0].id = 705;
        outsider.group = 0;
        outsider.effects.clear();
        project.tracks.push_back(outsider);
        project.groups[0].solo = true;
        expect(motion::trackIsAudible(project, project.tracks[0]));
        expect(!motion::trackIsAudible(project, project.tracks[1]));
        expectEquals(static_cast<int>(motion::PreparedComposition(project).clips.size()), 1);
        project.groups[0].muted = true;
        project.tracks.change(0).solo = true;
        expect(!motion::trackIsAudible(project, project.tracks[0]));
        expect(motion::PreparedComposition(project).clips.empty());
        project.groups[0].muted = false;
        project.groups[0].solo = false;
        project.tracks.change(0).solo = false;
        project.groups[1].solo = true;
        expect(motion::trackIsAudible(project, project.tracks[0]));
        expect(!motion::trackIsAudible(project, project.tracks[1]));

        beginTest("Group persistence and undo preserve nested values, IDs and shared assets");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(sourceProject);
        document.edit("Group tracks", [&](motion::Project& value) { value = project; });
        expect(undo.undo());
        expect(document.project().groups.empty());
        expect(undo.redo());
        expect(document.project().assets[0] == sourceProject.assets[0]);
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto xml = document.save();
        const auto loaded = restored.load(xml);
        expect(loaded.wasOk(), loaded.getErrorMessage());
        if (loaded.failed()) {
            return;
        }
        expect(restored.newId() > outsider.clips[0].id);
        expectEquals(static_cast<int>(restored.project().groups.size()), 2);
        expectEquals(static_cast<int>(restored.project().tracks[0].group), static_cast<int>(inner.id));
        expect(restored.project().groups[1].solo);
        motion::PreparedComposition reloaded(restored.project());
        expectWithinAbsoluteError(reloaded.clips[0].sample(3, 0.2).x, world.x, 0.00001f);
        const auto unchanged = restored.save().toString();
        const auto reject = [&](juce::XmlElement invalid) {
            expect(restored.load(invalid).failed());
            expectEquals(restored.save().toString(), unchanged);
        };
        auto cyclic = xml;
        cyclic.getChildByName("group")->setAttribute("parent", juce::String(inner.id));
        reject(cyclic);
        auto missing = xml;
        missing.getChildByName("group")->setAttribute("parent", "9999");
        reject(missing);
        auto missingTrackGroup = xml;
        missingTrackGroup.getChildByName("track")->setAttribute("group", "9999");
        reject(missingTrackGroup);
        auto duplicate = xml;
        duplicate.getChildByName("group")->setAttribute("id", juce::String(sourceProject.assets[0]->id));
        reject(duplicate);
        auto badCurve = xml;
        badCurve.getChildByName("group")->getChildByName("property")->setAttribute("name", "unknown");
        reject(badCurve);

        beginTest("Group nesting is bounded and malformed snapshots fail closed");
        auto deep = sourceProject;
        for (motion::Id index = 1; index <= 33; ++index) {
            motion::Group group;
            group.id = 1000 + index;
            group.parent = index == 1 ? 0 : group.id - 1;
            deep.groups.push_back(group);
        }
        deep.tracks.change(0).group = deep.groups.back().id;
        expect(!motion::validGroupHierarchy(deep));
        expect(motion::PreparedComposition(deep).clips.empty());
        deep.groups.pop_back();
        deep.tracks.change(0).group = deep.groups.back().id;
        expect(motion::validGroupHierarchy(deep));
        expectEquals(static_cast<int>(motion::PreparedComposition(deep).clips.size()), 1);
    }

    void testTrackStates(const motion::Project& sourceProject) {
        beginTest("Mute and solo exclude tracks from signal and drawing allocation");
        auto project = sourceProject;
        auto& original = project.tracks.change(0).clips[0];
        original.properties["red"] = motion::Curve(1);
        original.properties["green"] = motion::Curve(0);
        original.properties["blue"] = motion::Curve(0);
        auto secondTrack = project.tracks[0];
        secondTrack.id = 500;
        secondTrack.name = "Second";
        secondTrack.clips[0].id = 501;
        secondTrack.clips[0].properties["red"] = motion::Curve(0);
        secondTrack.clips[0].properties["green"] = motion::Curve(1);
        project.tracks.push_back(secondTrack);
        const motion::PreparedComposition both(project);
        expectEquals(static_cast<int>(both.clips.size()), 2);
        expectEquals(both.sample(1, 0.25).r, 1.0f);
        expectEquals(both.sample(1, 0.75).g, 1.0f);
        project.tracks.change(1).muted = true;
        const motion::PreparedComposition muted(project);
        expectEquals(static_cast<int>(muted.clips.size()), 1);
        expectEquals(muted.sample(1, 0.75).r, 1.0f);
        expectEquals(muted.sample(1, 0.75).g, 0.0f);
        expectEquals(both.sample(1, 0.75).g, 1.0f);

        project.tracks.change(1).muted = false;
        project.tracks.change(1).solo = true;
        const motion::PreparedComposition solo(project);
        expectEquals(static_cast<int>(solo.clips.size()), 1);
        expectEquals(solo.sample(1, 0.25).g, 1.0f);
        project.tracks.change(1).muted = true;
        const motion::PreparedComposition mutedSolo(project);
        expect(mutedSolo.clips.empty());
        const auto dark = mutedSolo.sample(1, 0.25);
        expectEquals(dark.r + dark.g + dark.b, 0.0f);
        project.tracks.change(0).solo = true;
        const motion::PreparedComposition multipleSolo(project);
        expectEquals(static_cast<int>(multipleSolo.clips.size()), 1);
        expectEquals(multipleSolo.sample(1, 0.75).r, 1.0f);

        beginTest("Lock leaves playback unchanged and excluded tracks consume no fade budget");
        project.tracks.change(0).locked = true;
        project.tracks.change(0).clips[0].properties["weight"] = motion::Curve(0.25);
        const motion::PreparedComposition locked(project);
        int litSamples = 0;
        for (int index = 0; index < 1000; ++index) {
            litSamples += locked.sample(1, index / 1000.0).r > 0 ? 1 : 0;
        }
        expectEquals(litSamples, 250);
        project.tracks.change(1).muted = false;
        const motion::PreparedComposition bothSolo(project);
        expectEquals(static_cast<int>(bothSolo.clips.size()), 2);

        beginTest("Track state survives save, load, undo and redo without copying assets");
        project.tracks.change(1).muted = true;
        const motion::PreparedComposition savedSignal(project);
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(sourceProject);
        document.edit("Track controls", [&](motion::Project& value) { value = project; });
        expect(undo.undo());
        expect(!document.project().tracks[0].muted && !document.project().tracks[0].solo && !document.project().tracks[0].locked);
        expect(document.project().assets[0] == sourceProject.assets[0]);
        expect(undo.redo());
        expect(document.project().tracks[0].solo && document.project().tracks[0].locked);
        expect(document.project().assets[0] == sourceProject.assets[0]);
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto loaded = restored.load(document.save());
        expect(loaded.wasOk(), loaded.getErrorMessage());
        if (loaded.wasOk()) {
            expect(restored.project().tracks[0].solo && restored.project().tracks[0].locked);
            expect(!restored.project().tracks[0].muted && !restored.project().tracks[1].locked);
            expect(restored.project().tracks[1].solo && restored.project().tracks[1].muted);
            const motion::PreparedComposition reloaded(restored.project());
            expectEquals(reloaded.sample(1, 0.1).r, savedSignal.sample(1, 0.1).r);
            expectEquals(reloaded.sample(1, 0.5).g, savedSignal.sample(1, 0.5).g);
        }
    }

    void testRoundTripKeyEditing(const motion::Project& sourceProject) {
        beginTest("Saved source-time keys remain editable without floating-point duplicates");
        motion::Project project; project.assets = sourceProject.assets; project.duration = 185.6;
        auto clip = motion::Document::makeClip(90001, *project.assets.front(), 128);
        clip.duration = 51.2;
        clip.properties["rotation.y"].setKey({166.4 - 128, 0, motion::Interpolation::cubic, 2, -3});
        motion::Track track; track.id = 90002; track.clips = {clip}; project.tracks = {track};
        juce::UndoManager undo; motion::Document document(undo); document.reset(project);
        for (int pass = 0; pass < 4; ++pass) {
            const auto saved = document.save();
            const auto loaded = document.load(saved);
            expect(loaded.wasOk(), loaded.getErrorMessage());
            document.edit("Edit restored key", [pass](motion::Project& value) {
                auto& clip = value.tracks.change(0).clips.front();
                clip.properties.at("rotation.y").setKeyValue(clip.localTime(166.4, motion::Tempo(120)), 90 + pass);
            });
            const auto& curve = document.project().tracks.front().clips.front().properties.at("rotation.y");
            expectEquals(static_cast<int>(curve.keyframes().size()), 1);
            expectEquals(curve.evaluateBase(166.4 - 128), 90.0 + pass);
            expect(curve.keyframes().front().interpolation == motion::Interpolation::cubic);
            expectEquals(curve.keyframes().front().incomingSlope, 2.0);
            expectEquals(curve.keyframes().front().outgoingSlope, -3.0);
            expect(undo.undo()); expect(undo.redo());
        }
    }

    void testCompositionCreation(const motion::Project& sourceProject) {
        beginTest("Creating a composition preserves partial groups, authored clocks and shared media");
        motion::Project project; project.assets = sourceProject.assets; project.duration = 20;
        auto clip = motion::Document::makeClip(70001, *project.assets.front(), 2); clip.duration = 4;
        auto sibling = clip; sibling.id = 70002; sibling.start = 10; sibling.duration = 2;
        motion::Track track; track.id = 70003; track.clips = {clip, sibling}; track.group = 70004;
        motion::Group group; group.id = 70004;
        group.properties["position.x"].setKey({0, 0, motion::Interpolation::linear});
        group.properties["position.x"].setKey({20, 2, motion::Interpolation::linear});
        auto effect = motion::makeEffect(70005, *motion::effectDefinition("translate"));
        effect.properties["translateY"].setKey({0, 0, motion::Interpolation::linear});
        effect.properties["translateY"].setKey({20, 1, motion::Interpolation::linear});
        track.effects = {effect}; project.tracks = {track}; project.groups = {group};
        auto global = motion::makeEffect(70006, *motion::effectDefinition("scale")); project.effects = {global};
        juce::UndoManager undo; motion::Document document(undo); document.reset(project);
        const motion::PreparedComposition before(project);
        motion::Id instance = 0;
        const auto created = document.createComposition({clip.id}, "Motif", instance);
        expect(created.wasOk(), created.getErrorMessage());
        if (created.failed()) { return; }
        expect(instance != 0 && document.project().definitions.size() == 1);
        const auto& definition = *document.project().definitions.front();
        expectEquals(static_cast<int>(definition.tracks.size()), 1);
        expectEquals(static_cast<int>(definition.groups.size()), 1);
        expect(definition.tracks.front().clips.front().id == clip.id);
        expect(definition.groups.front().id != group.id && definition.tracks.front().group == definition.groups.front().id);
        expect(definition.tracks.front().effects.front().id != effect.id);
        expect(definition.tracks.front().clips.front().asset == clip.asset);
        expect(document.project().assets.front() == project.assets.front());
        const auto& placement = document.project().tracks.front().clips.front();
        expectEquals(placement.start, 2.0); expectEquals(placement.offset, 2.0); expectEquals(placement.duration, 4.0);
        expect(document.project().tracks[1].clips.front().id == sibling.id);
        const motion::PreparedComposition after(document.project());
        expect(after.preparationError.isEmpty(), after.preparationError);
        for (const auto time : {2.25, 3.5, 5.5, 10.5}) {
            for (const auto phase : {0.12, 0.37, 0.68}) {
                const auto a = before.sample(time, phase), b = after.sample(time, phase);
                expectWithinAbsoluteError(b.x, a.x, 0.00001f); expectWithinAbsoluteError(b.y, a.y, 0.00001f);
                expectWithinAbsoluteError(b.r, a.r, 0.00001f); expectWithinAbsoluteError(b.g, a.g, 0.00001f);
            }
        }
        juce::UndoManager loadedUndo; motion::Document loaded(loadedUndo);
        const auto reopened = loaded.load(document.save());
        expect(reopened.wasOk(), reopened.getErrorMessage());
        expect(undo.undo()); expect(document.project().definitions.empty());
        expect(document.project().tracks.front().clips.size() == 2);
        expect(undo.redo()); expect(document.project().definitions.size() == 1);
        expect(document.project().tracks.front().clips.front().id == instance);
        beginTest("Making an instance unique forks authored identities while sharing media");
        juce::UndoManager uniqueUndo; motion::Document unique(uniqueUndo); unique.reset(document.mainProject());
        const auto cameraId = unique.newId(), cutId = unique.newId();
        unique.edit("Add definition camera", [cameraId, cutId](motion::Project& value) {
            auto definition = std::make_shared<motion::CompositionDefinition>(*value.definitions.front());
            motion::Camera camera; camera.id = cameraId; definition->cameras = {camera};
            motion::CameraCut cut; cut.id = cutId; cut.camera = cameraId; cut.start = 0; cut.duration = 20;
            definition->cameraCuts = {cut}; value.definitions.front() = definition;
        });
        motion::Id secondInstance = 0; expect(unique.duplicateClip(instance, secondInstance).wasOk());
        const auto originalDefinition = unique.mainProject().definitions.front();
        motion::Id copyId = 0;
        const auto forked = unique.makeCompositionUnique(secondInstance, copyId);
        expect(forked.wasOk(), forked.getErrorMessage());
        if (forked.wasOk()) {
            expect(unique.mainProject().definitions.size() == 2 && copyId != originalDefinition->id);
            const auto copy = unique.mainProject().definitions.back();
            expect(copy->tracks.front().id != originalDefinition->tracks.front().id);
            expect(copy->tracks.front().clips.front().id != originalDefinition->tracks.front().clips.front().id);
            expect(copy->tracks.front().clips.front().asset == originalDefinition->tracks.front().clips.front().asset);
            expect(copy->groups.front().id != originalDefinition->groups.front().id);
            expect(copy->tracks.front().group == copy->groups.front().id);
            expect(copy->cameras.front().id != cameraId && copy->cameraCuts.front().camera == copy->cameras.front().id);
            expect(copy->cameraCuts.front().id != cutId);
            expect(unique.project().tracks.front().clips.front().composition == originalDefinition->id);
            expect(unique.project().tracks.front().clips.back().composition == copyId);
            const auto copySaved = loaded.load(unique.save()); expect(copySaved.wasOk(), copySaved.getErrorMessage());
            expect(uniqueUndo.undo()); expect(unique.mainProject().definitions.size() == 1);
            expect(uniqueUndo.redo()); expect(unique.mainProject().definitions.size() == 2);
            expect(unique.enterComposition(copyId).wasOk());
            unique.edit("Edit isolated child", [](motion::Project& value) { value.tracks.change(0).clips.front().properties["position.z"] = motion::Curve(0.4); });
            expectWithinAbsoluteError(unique.mainProject().definitions.front()->tracks.front().clips.front().properties.at("position.z").base, 0.0, 0.00001);
            expect(unique.mainProject().assets.front() == project.assets.front());
            const auto isolatedSaved = loaded.load(unique.save()); expect(isolatedSaved.wasOk(), isolatedSaved.getErrorMessage());
            expect(unique.enterComposition(0).wasOk());
            motion::Id inserted = 0;
            const auto insertion = unique.insertComposition(originalDefinition->id, 20, 0, 0, inserted);
            expect(insertion.wasOk(), insertion.getErrorMessage());
            expect(unique.project().tracks.back().clips.front().id == inserted);
            expectEquals(unique.project().tracks.back().clips.front().offset, 2.0);
            expectEquals(unique.project().tracks.back().clips.front().duration, 4.0);
            expect(uniqueUndo.undo()); expect(uniqueUndo.redo());
            const auto insertedSaved = loaded.load(unique.save()); expect(insertedSaved.wasOk(), insertedSaved.getErrorMessage());
            unique.edit("Lock insertion track", [](motion::Project& value) { value.tracks.change(value.tracks.size() - 1).locked = true; });
            const auto revision = unique.revision();
            const auto highest = motion::highestProjectIdentity(unique.mainProject());
            expect(unique.insertComposition(originalDefinition->id, 30, unique.project().tracks.back().id, 0, inserted).failed());
            expect(inserted == 0 && unique.revision() == revision && unique.newId() == highest + 1);
            expect(unique.enterComposition(originalDefinition->id).wasOk());
            expect(!unique.canReferenceComposition(originalDefinition->id));
            const auto scopeRevision = unique.revision();
            expect(unique.insertComposition(originalDefinition->id, 0, 0, 0, inserted).failed());
            expect(inserted == 0 && unique.revision() == scopeRevision);
        }
        beginTest("Unused definition removal is reference-safe, scoped and undoable");
        juce::UndoManager cleanupUndo; motion::Document cleanup(cleanupUndo); cleanup.reset(unique.mainProject());
        const auto referenced = cleanup.mainProject().definitions.front()->id;
        expect(cleanup.compositionReferenceCount(referenced) > 0);
        const auto beforeRejected = cleanup.revision();
        expect(cleanup.removeComposition(referenced).failed());
        expect(cleanup.revision() == beforeRejected && !cleanupUndo.canUndo());
        cleanup.edit("Remove independent instances", [copyId](motion::Project& value) {
            value.tracks.changeAll([copyId](motion::Track& track) { std::erase_if(track.clips, [copyId](const auto& clip) { return clip.composition == copyId; }); });
        });
        expectEquals(static_cast<int>(cleanup.compositionReferenceCount(copyId)), 0);
        expect(cleanup.enterComposition(copyId).wasOk());
        expect(cleanup.removeComposition(copyId).failed());
        expect(cleanup.enterComposition(referenced).wasOk());
        const auto sourceCount = cleanup.mainProject().assets.size();
        expect(cleanup.removeComposition(copyId).wasOk());
        expect(cleanup.editingComposition() == referenced && cleanup.mainProject().definitions.size() == 1);
        expect(cleanup.mainProject().assets.size() == sourceCount);
        expect(cleanupUndo.undo()); expect(cleanup.mainProject().definitions.size() == 2);
        expect(cleanupUndo.redo()); expect(cleanup.mainProject().definitions.size() == 1);
        const auto cleanedSaved = loaded.load(cleanup.save()); expect(cleanedSaved.wasOk(), cleanedSaved.getErrorMessage());
        beginTest("Nested editing uses one undo history while save retains the complete main project");
        const auto definitionId = document.project().definitions.front()->id;
        const auto mainName = document.mainProject().name;
        const auto generation = document.generation();
        expect(document.enterComposition(definitionId).wasOk());
        expect(document.generation() != generation && document.editingComposition() == definitionId);
        expect(document.project().tracks.front().clips.front().id == clip.id);
        document.edit("Rename motif", [](motion::Project& value) { value.name = "Shared edited motif"; });
        expect(document.project().name == "Shared edited motif");
        expect(document.mainProject().name == mainName);
        expect(document.mainProject().definitions.front()->name == "Shared edited motif");
        const auto savedInside = loaded.load(document.save());
        expect(savedInside.wasOk(), savedInside.getErrorMessage());
        expect(loaded.project().tracks.front().clips.front().composition == definitionId);
        expect(loaded.project().definitions.front()->name == "Shared edited motif");
        expect(undo.undo()); expect(document.project().name == "Motif");
        expect(undo.redo()); expect(document.project().name == "Shared edited motif");
        auto beforeDrag = document.project();
        auto preview = beforeDrag; preview.tracks.change(0).clips.front().properties["position.z"] = motion::Curve(0.3);
        document.preview(std::move(preview)); document.commit("Move child", beforeDrag);
        expectWithinAbsoluteError(document.mainProject().definitions.front()->tracks.front().clips.front().properties.at("position.z").base, 0.3, 0.00001);
        expect(undo.undo());
        expectWithinAbsoluteError(document.project().tracks.front().clips.front().properties.at("position.z").base, 0.0, 0.00001);
        motion::Id nestedInstance = 0;
        expect(document.createComposition({clip.id}, "Inner motif", nestedInstance).wasOk());
        expect(document.mainProject().definitions.size() == 2);
        expect(document.mainProject().tracks.front().clips.front().id == instance);
        expect(document.project().tracks.front().clips.front().id == nestedInstance);
        const auto nestedSaved = loaded.load(document.save());
        expect(nestedSaved.wasOk(), nestedSaved.getErrorMessage());
        const auto innerId = document.project().tracks.front().clips.front().composition;
        expect(document.enterComposition(innerId).wasOk());
        expect(!document.canReferenceComposition(definitionId), "An ancestor cannot be inserted inside its child");
        motion::Id rejectedInsertion = 123;
        expect(document.insertComposition(definitionId, 0, 0, 0, rejectedInsertion).failed());
        expect(rejectedInsertion == 0);
        expect(document.enterComposition(definitionId).wasOk());
        expect(undo.undo()); expect(document.mainProject().definitions.size() == 1);
        expect(undo.undo()); // shared rename
        expect(undo.undo()); // initial creation; active definition disappears
        expect(document.editingComposition() == 0 && document.project().definitions.empty());
        expect(document.project().tracks.front().clips.size() == 2);
        expect(document.enterComposition(999999).failed());
        beginTest("Invalid precomposition leaves identity allocation and undo untouched");
        for (int reason = 0; reason < 4; ++reason) {
            auto invalid = project; if (reason == 0) { invalid.tracks.change(0).locked = true; }
            juce::UndoManager rejectedUndo; motion::Document rejected(rejectedUndo); rejected.reset(invalid);
            const auto revision = rejected.revision(); motion::Id output = 999;
            const auto ids = reason == 1 ? std::vector<motion::Id>{999999} : reason == 2 ? std::vector<motion::Id>{clip.id, clip.id} : std::vector<motion::Id>{clip.id};
            expect(rejected.createComposition(ids, reason == 3 ? "  " : "Motif", output).failed());
            expect(output == 0 && rejected.revision() == revision && !rejectedUndo.canUndo());
            expect(rejected.newId() == 70007);
        }
        beginTest("Precomposition preserves solo-filtered visibility across scopes");
        project.tracks.change(0).solo = true;
        auto hidden = project.tracks[0]; hidden.id = 70010; hidden.solo = false; hidden.group = 0; hidden.effects.clear();
        hidden.clips = {clip}; hidden.clips[0].id = 70011;
        project.tracks.push_back(hidden);
        document.reset(project);
        expect(document.createComposition({clip.id, 70011}, "Solo study", instance).wasOk());
        const motion::PreparedComposition solo(document.project());
        expect(solo.preparationError.isEmpty(), solo.preparationError);
        expect(std::none_of(solo.clips.begin(), solo.clips.end(), [](const auto& value) { return value.id == 70011; }));
        expect(document.project().tracks.front().solo);
    }

    void testCompositions(const motion::Project& sourceProject) {
        beginTest("Reusable definitions round trip shared media and globally unique nested identities");
        auto project = sourceProject;
        auto definition = std::make_shared<motion::CompositionDefinition>();
        definition->id = 60001; definition->name = "Reusable motif";
        auto childTrack = project.tracks[0];
        childTrack.id = 60002; childTrack.clips.resize(1); childTrack.effects.clear(); childTrack.group = 0;
        childTrack.clips[0].id = 60003; childTrack.clips[0].effects.clear();
        definition->tracks = {childTrack};
        project.definitions = {definition};
        const auto sharedCount = motion::sourceReferenceCount(project, childTrack.clips[0].asset);
        expect(sharedCount > motion::sourceReferenceCount(sourceProject, childTrack.clips[0].asset));
        auto& instance = project.tracks.change(0).clips[0];
        instance.asset = 0; instance.composition = definition->id;
        juce::UndoManager undo, loadedUndo;
        motion::Document document(undo), loaded(loadedUndo);
        document.reset(project);
        const auto xml = document.save();
        const auto result = loaded.load(xml);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) { return; }
        expect(loaded.project().definitions.size() == 1);
        expect(loaded.project().assets.size() == project.assets.size());
        expect(loaded.project().tracks[0].clips[0].composition == definition->id);
        expect(loaded.project().definitions[0]->tracks[0].clips[0].asset == childTrack.clips[0].asset);
        expect(loaded.newId() > 60003);
        const motion::PreparedComposition prepared(loaded.project());
        expect(prepared.preparationError.isEmpty(), prepared.preparationError);
        expect(!prepared.clips.empty());
        beginTest("Nested visual stages retain local clocks, transform order and drawing fades");
        motion::Project visual;
        visual.assets = sourceProject.assets;
        auto motif = std::make_shared<motion::CompositionDefinition>();
        motif->id = 61000; motif->duration = 10;
        auto child = motion::Document::makeClip(61001, *visual.assets.front(), 1);
        child.duration = 6;
        child.properties["position.x"].setKey({0, 0, motion::Interpolation::linear});
        child.properties["position.x"].setKey({10, 10, motion::Interpolation::linear});
        child.properties["weight"] = motion::Curve(0.5);
        motion::Track nestedTrack; nestedTrack.id = 61002; nestedTrack.clips = {child};
        auto translate = motion::makeEffect(61003, *motion::effectDefinition("translate"));
        translate.properties["translateX"] = motion::Curve(0.25);
        nestedTrack.effects = {translate};
        motion::Group group; group.id = 61004; group.properties["scale.x"] = motion::Curve(2);
        nestedTrack.group = group.id; motif->groups = {group}; motif->tracks = {nestedTrack};
        translate.id = 61005; translate.properties["translateX"] = motion::Curve(0.5); motif->effects = {translate};
        visual.definitions = {motif};
        motion::Clip placement; placement.id = 61006; placement.composition = motif->id;
        placement.start = 4; placement.duration = 3; placement.offset = 2; placement.rate = 2;
        placement.properties["position.x"] = motion::Curve(1);
        placement.properties["scale.x"] = motion::Curve(3);
        placement.properties["weight"] = motion::Curve(0.25);
        motion::Track mainTrack; mainTrack.id = 61007; mainTrack.clips = {placement}; visual.tracks = {mainTrack};
        const motion::PreparedComposition nestedVisual(visual);
        expect(nestedVisual.preparationError.isEmpty(), nestedVisual.preparationError);
        expectEquals(static_cast<int>(nestedVisual.clips.size()), 1);
        if (!nestedVisual.clips.empty()) {
            const auto& leaf = nestedVisual.clips.front();
            expectWithinAbsoluteError(leaf.processPoint({0, 0, 0, 1, 1, 1}, 5).x, 22.0f, 0.0001f);
            expectWithinAbsoluteError(leaf.weight(5), 0.125, 0.000001);
            expect(!leaf.active(3.99) && leaf.active(4) && !leaf.active(6.5));
            expect(nestedVisual.selectBeam(5, 0.2).clip == nullptr, "Outer fades retain unused dark allocation");
            expect(nestedVisual.selectBeam(5, 0.1).clip != nullptr);
            motion::BeamRenderer beam;
            const auto first = beam.sample(nestedVisual, 5, 240000, 48000, true, 1);
            beam.sample(nestedVisual, 4.5, 216000, 48000, true, 1);
            const auto again = beam.sample(nestedVisual, 5, 240000, 48000, true, 1);
            expectEquals(first.x, again.x); expectEquals(first.r, again.r);
        }
        auto repeat = placement; repeat.id = 61008; repeat.start = 10; repeat.rate = 1;
        visual.tracks.change(0).clips.push_back(repeat);
        const motion::PreparedComposition repeatedVisual(visual);
        expect(repeatedVisual.preparationError.isEmpty(), repeatedVisual.preparationError);
        expectEquals(static_cast<int>(repeatedVisual.clips.size()), 2);
        if (repeatedVisual.clips.size() == 2) {
            expect(repeatedVisual.clips[0].source == repeatedVisual.clips[1].source);
            expectWithinAbsoluteError(repeatedVisual.clips[1].processPoint({0, 0, 0, 1, 1, 1}, 11).x, 16.0f, 0.0001f);
        }
        beginTest("Repeated definitions stay visible across instance cuts");
        visual.tracks.change(0).clips[0].duration = 2;
        visual.tracks.change(0).clips[0].properties["weight"] = motion::Curve(1);
        visual.tracks.change(0).clips[1].start = 6;
        visual.tracks.change(0).clips[1].properties["weight"] = motion::Curve(1);
        motif->tracks.change(0).clips[0].properties["weight"] = motion::Curve(1);
        const motion::PreparedComposition cuts(visual);
        expect(cuts.preparationError.isEmpty(), cuts.preparationError);
        const auto after = std::find_if(cuts.clips.begin(), cuts.clips.end(), [](const auto& clip) { return clip.active(6.1); });
        expect(after != cuts.clips.end() && after->sample(6.1, 0.2).r > 0, "The later instance is active and lit after the instance cut");
        beginTest("Invalid reusable graph loads leave the existing document untouched");
        auto cyclic = xml;
        auto* nestedClip = cyclic.getChildByName("definition")->getChildByName("composition")->getChildByName("track")->getChildByName("clip");
        nestedClip->removeAttribute("asset"); nestedClip->setAttribute("composition", "60001");
        const auto revision = loaded.revision();
        expect(loaded.load(cyclic).failed());
        expect(loaded.revision() == revision && loaded.project().definitions.size() == 1);
        auto duplicate = xml;
        duplicate.getChildByName("definition")->setAttribute("id", juce::String(project.assets[0]->id));
        expect(loaded.load(duplicate).failed());
        expect(loaded.revision() == revision);
    }

    void testEffects(const motion::Project& sourceProject) {
        beginTest("Effect catalog uses shared stateless geometry and preserves RGB");
        const osci::Point input(0.25f, 0.5f, -0.2f, 0.3f, 0.6f, 0.9f);
        for (const auto& definition : motion::effectCatalog()) {
            auto effect = motion::makeEffect(100, definition);
            expect(effect.valid());
            motion::PreparedEffect prepared(effect);
            const auto output = prepared.apply(input, 0.4);
            const auto repeated = prepared.apply(input, 0.4);
            expect(std::isfinite(output.x) && std::isfinite(output.y) && std::isfinite(output.z));
            expectEquals(output.x, repeated.x);
            expectEquals(output.r, input.r);
            expectEquals(output.g, input.g);
            expectEquals(output.b, input.b);
            effect.properties["strength"] = motion::Curve(0);
            expectEquals(motion::PreparedEffect(effect).apply(input, 0.4).x, input.x);
            effect.enabled = false;
            effect.properties["strength"] = motion::Curve(1);
            expectEquals(motion::PreparedEffect(effect).apply(input, 0.4).x, input.x);
        }
        beginTest("Colour effects change signal RGB without changing geometry or illuminating blanking");
        auto colour = motion::makeEffect(190, *motion::effectDefinition("colour"));
        colour.properties["hue"] = motion::Curve(120);
        const osci::Point red(.25f, -.5f, .75f, 1, 0, 0);
        auto green = motion::PreparedEffect(colour).apply(red, 0);
        expectEquals(green.x, red.x); expectEquals(green.y, red.y); expectEquals(green.z, red.z);
        expectWithinAbsoluteError(green.r, 0.0f, 1e-6f);
        expectWithinAbsoluteError(green.g, 1.0f, 1e-6f);
        expectWithinAbsoluteError(green.b, 0.0f, 1e-6f);
        colour.properties["strength"] = motion::Curve(.5);
        const auto mixed = motion::PreparedEffect(colour).apply(red, 0);
        expectWithinAbsoluteError(mixed.r, .5f, 1e-6f); expectWithinAbsoluteError(mixed.g, .5f, 1e-6f);
        colour.properties["strength"] = motion::Curve(1);
        colour.properties["saturation"] = motion::Curve(0);
        colour.properties["brightness"] = motion::Curve(.25);
        const auto grey = motion::PreparedEffect(colour).apply(red, 0);
        expectEquals(grey.r, .25f); expectEquals(grey.g, .25f); expectEquals(grey.b, .25f);
        const auto blank = motion::PreparedEffect(colour).apply(red.withColour(0, 0, 0), 0);
        expectEquals(blank.r, 0.0f); expectEquals(blank.g, 0.0f); expectEquals(blank.b, 0.0f);
        expectEquals(blank.x, red.x);
        const auto inherited = motion::PreparedEffect(colour).apply(red.withColour(-1, -1, -1), 0);
        expectEquals(inherited.r, -1.0f); expectEquals(inherited.g, -1.0f); expectEquals(inherited.b, -1.0f);
        colour.properties["saturation"] = motion::Curve(1);
        colour.properties["brightness"] = motion::Curve(1);
        colour.properties["hue"] = motion::Curve(0);
        colour.properties["hue"].setKey({0, 0, motion::Interpolation::linear});
        colour.properties["hue"].setKey({2, 120, motion::Interpolation::linear});
        const auto yellow = motion::PreparedEffect(colour).apply(red, 1);
        expectWithinAbsoluteError(yellow.r, 1.0f, 1e-6f); expectWithinAbsoluteError(yellow.g, 1.0f, 1e-6f);
        beginTest("Clip colour is resolved before colour effects and saved animated stacks reopen");
        auto colouredProject = sourceProject;
        auto& colouredClip = colouredProject.tracks.change(0).clips[0];
        colouredClip.start = 0; colouredClip.offset = 0; colouredClip.rate = 1;
        colouredClip.properties["red"] = motion::Curve(1);
        colouredClip.properties["green"] = motion::Curve(0);
        colouredClip.properties["blue"] = motion::Curve(0);
        colouredClip.effects = {colour};
        const motion::PreparedComposition coloured(colouredProject);
        const auto signal = coloured.clips[0].processPoint(red.withColour(-1, -1, -1), 2);
        expectWithinAbsoluteError(signal.r, 0.0f, 1e-6f); expectWithinAbsoluteError(signal.g, 1.0f, 1e-6f);
        juce::UndoManager colourUndo, restoredColourUndo;
        motion::Document colourDocument(colourUndo), restoredColour(restoredColourUndo);
        colourDocument.reset(colouredProject);
        expect(restoredColour.load(colourDocument.save()).wasOk());
        const motion::PreparedComposition restoredSignal(restoredColour.project());
        const auto reopenedColour = restoredSignal.clips[0].processPoint(red.withColour(-1, -1, -1), 2);
        expectEquals(reopenedColour.r, signal.r); expectEquals(reopenedColour.g, signal.g); expectEquals(reopenedColour.b, signal.b);
        auto translate = motion::makeEffect(101, *motion::effectDefinition("translate"));
        translate.properties["translateX"] = motion::Curve(0.5);
        expectWithinAbsoluteError(motion::PreparedEffect(translate).apply(input, 0).x, 0.75f, 0.000001f);
        auto rotate = motion::makeEffect(102, *motion::effectDefinition("rotate"));
        rotate.properties["rotateZ"] = motion::Curve(0.5);
        const auto rotated = motion::PreparedEffect(rotate).apply(input, 0);
        expectWithinAbsoluteError(rotated.x, -input.y, 0.000001f);
        expectWithinAbsoluteError(rotated.y, input.x, 0.000001f);
        auto scale = motion::makeEffect(103, *motion::effectDefinition("scale"));
        scale.properties["scaleX"] = motion::Curve(2);
        scale.properties["scaleY"] = motion::Curve(1);
        scale.properties["scaleZ"] = motion::Curve(1);
        const auto firstOrder = motion::applyEffects(motion::prepareEffects({ translate, scale }), input, 0);
        const auto reverseOrder = motion::applyEffects(motion::prepareEffects({ scale, translate }), input, 0);
        expectWithinAbsoluteError(firstOrder.x, 1.5f, 0.000001f);
        expectWithinAbsoluteError(reverseOrder.x, 1.0f, 0.000001f);
        translate.range = motion::EffectRange { -1, 2 };
        const motion::PreparedEffect ranged(translate);
        expectEquals(ranged.apply(input, -1.01).x, input.x);
        expectWithinAbsoluteError(ranged.apply(input, -1).x, 0.75f, 0.000001f);
        expectEquals(ranged.apply(input, 1).x, input.x);

        beginTest("Clip, track and composition stacks run in scope order with owner clocks");
        auto project = sourceProject;
        auto& clip = project.tracks.change(0).clips[0];
        clip.start = 2;
        clip.duration = 8;
        clip.offset = 0.5;
        clip.rate = 2;
        clip.properties["position.x"] = motion::Curve(0.25);
        clip.properties["scale.x"] = motion::Curve(2);
        translate.range.reset();
        translate.properties["translateX"] = motion::Curve(0);
        translate.properties["translateX"].setKey({0, 0, motion::Interpolation::linear});
        translate.properties["translateX"].setKey({10, 1});
        clip.effects = { translate };
        scale.id = 104;
        project.tracks.change(0).effects = { scale };
        auto global = motion::makeEffect(105, *motion::effectDefinition("translate"));
        global.properties["translateX"] = motion::Curve(0.1);
        project.effects = { global };
        motion::PreparedComposition composition(project);
        const auto local = clip.localTime(3, motion::Tempo(120));
        const auto raw = sourceProject.assets[0]->source->sample(local, 0.2);
        const auto expectedWorldX = ((raw.x + local / 10.0) * 2 + 0.25) * 2;
        const auto world = composition.clips[0].sample(3, 0.2);
        expectWithinAbsoluteError(world.x, static_cast<float>(expectedWorldX), 0.00001f);
        const auto output = composition.projectPoint(world, 3);
        expectWithinAbsoluteError(output.x, static_cast<float>((expectedWorldX + 0.1) * 4 / (4 - world.z)), 0.00001f);
        const auto clipTarget = motion::findPropertyTarget(project, translate.id);
        expect(clipTarget.has_value() && clipTarget->isEffect && !clipTarget->camera);
        if (clipTarget.has_value()) {
            expectEquals(clipTarget->localTime(3), local);
        }
        const auto trackTarget = motion::findPropertyTarget(project, scale.id);
        expect(trackTarget.has_value() && trackTarget->isEffect);
        if (trackTarget.has_value()) {
            expectEquals(trackTarget->localTime(3), 3.0);
        }
        expect(motion::findEffectOwner(project, 0) == &project.effects);
        expect(motion::findEffectOwner(project, project.tracks[0].id) == &project.tracks[0].effects);
        expect(motion::findEffect(project, global.id) == &project.effects[0]);

        beginTest("Effect order, curves and time ranges round trip, sharing assets through undo");
        clip.effects[0].range = motion::EffectRange { -0.5, 6 };
        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(sourceProject);
        document.edit("Add effects", [&](motion::Project& value) { value = project; });
        expect(undo.undo());
        expect(document.project().effects.empty());
        expect(document.project().assets[0] == sourceProject.assets[0]);
        expect(undo.redo());
        expect(document.project().assets[0] == sourceProject.assets[0]);
        juce::UndoManager loadedUndo;
        motion::Document loaded(loadedUndo);
        const auto xml = document.save();
        const auto result = loaded.load(xml);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) {
            return;
        }
        expect(loaded.newId() > global.id);
        const auto* restoredEffect = motion::findEffect(loaded.project(), translate.id);
        expect(restoredEffect != nullptr && restoredEffect->range.has_value());
        motion::PreparedComposition restored(loaded.project());
        expectWithinAbsoluteError(restored.sample(3, 0.2).x, composition.sample(3, 0.2).x, 0.00001f);
        const auto before = loaded.save().toString();
        const auto reject = [&](juce::XmlElement invalid) {
            expect(loaded.load(invalid).failed());
            expectEquals(loaded.save().toString(), before);
        };
        auto duplicate = xml;
        duplicate.getChildByName("effect")->setAttribute("id", juce::String(sourceProject.assets[0]->id));
        reject(duplicate);
        auto unknown = xml;
        unknown.getChildByName("effect")->setAttribute("type", "not-an-effect");
        reject(unknown);
        auto badRange = xml;
        badRange.getChildByName("effect")->setAttribute("start", 0);
        badRange.getChildByName("effect")->setAttribute("duration", -1);
        reject(badRange);
        auto badParameter = xml;
        badParameter.getChildByName("effect")->getChildByName("property")->setAttribute("base", 10000);
        reject(badParameter);
    }

    static motion::Asset textAsset(const juce::String& extension, const juce::String& text) {
        motion::Asset asset;
        asset.id = 1;
        asset.name = "Animation";
        asset.extension = extension;
        asset.data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
        return asset;
    }

    void testAnimatedSources() {
        const juce::String frame = R"json({"focalLength":1,"objects":[{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"vertices":[[{"x":0.1,"y":0,"z":-1},{"x":0.4,"y":0,"z":-1}]]}]})json";
        const auto json = "{\"frames\":[" + frame + "," + frame.replace("0.1", "-0.4").replace("0.4", "0.2") + "]}";
        beginTest("GPLA immutable frames loop in clip-local seconds, including negative offsets");
        auto asset = std::make_shared<motion::Asset>(textAsset(".gpla", json));
        std::atomic<double> progress { -1.0 };
        const auto result = motion::decodeAsset(*asset, nullptr, &progress);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) {
            return;
        }
        expectEquals(progress.load(), 1.0);
        expectEquals(static_cast<int>(asset->source->frameCount()), 2);
        expectEquals(asset->source->frameRate(), 30.0);
        expectWithinAbsoluteError(asset->source->duration(), 2.0 / 30.0, 1.0e-12);
        expectEquals(static_cast<int>(asset->source->frameIndex(-0.5 / 30.0)), 1);
        const auto first = asset->source->sample(0, 0.3);
        const auto second = asset->source->sample(1.5 / 30.0, 0.3);
        expect(std::abs(first.x - second.x) > 0.01f);
        expectEquals(asset->source->sample(2.0 / 30.0, 0.3).x, first.x);
        auto clip = motion::Document::makeClip(2, *asset, 0);
        expectEquals(clip.duration, asset->source->duration());
        clip.duration = 2;
        clip.offset = -0.5 / 30.0;
        clip.rate = 2;
        motion::Project project;
        project.assets.push_back(asset);
        motion::Track track;
        track.id = 3;
        track.clips.push_back(clip);
        project.tracks.push_back(track);
        motion::PreparedComposition composition(project);
        expectEquals(composition.clips[0].sample(0, 0.3).x, second.x);
        expectEquals(composition.clips[0].sample(0.5 / 30.0, 0.3).x, first.x);
        auto independent = clip;
        independent.id = 4;
        independent.start = 3;
        independent.offset = 0;
        expect(independent.stretch(4, motion::Tempo(120)));
        project.tracks.change(0).clips.push_back(independent);
        motion::PreparedComposition instances(project);
        expect(instances.clips[0].source == instances.clips[1].source);
        expectEquals(instances.clips[1].sample(3, 0.3).x, first.x);
        const auto beforeTrim = instances.clips[1].sample(3.05, 0.3).x;
        expect(project.tracks.change(0).clips[1].trim(3.05, 7, motion::Tempo(120)));
        motion::PreparedComposition trimmed(project);
        expectWithinAbsoluteError(trimmed.clips[1].sample(3.05, 0.3).x, beforeTrim, 0.000001f);


        beginTest("Animated source round trips reprepare shared payload and retain clip timing");
        juce::UndoManager undo;
        motion::Document document(undo);
        document.edit("Import animation", [&](motion::Project& destination) { destination = project; });
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto loaded = restored.load(document.save());
        expect(loaded.wasOk(), loaded.getErrorMessage());
        if (loaded.wasOk()) {
            motion::PreparedComposition restoredComposition(restored.project());
            expectEquals(restoredComposition.clips[0].sample(0, 0.3).x, second.x);
            expectWithinAbsoluteError(restored.project().tracks[0].clips[0].offset, clip.offset, 1.0e-12);
            expectEquals(restored.project().tracks[0].clips[0].rate, clip.rate);
        }
        expect(undo.undo());
        expect(undo.redo());
        expect(document.project().assets[0] == asset);

        beginTest("Blank animation frames remain dark and malformed or cancelled imports are atomic");
        auto blank = textAsset(".gpla", "{\"frames\":[" + frame + ",{\"focalLength\":1,\"objects\":[]}]}");
        expect(motion::decodeAsset(blank).wasOk());
        if (blank.source != nullptr) {
            const auto dark = blank.source->sample(1.5 / 30.0, 0.3);
            expectEquals(dark.r + dark.g + dark.b, 0.0f);
        }
        const auto retained = asset->source;
        std::atomic<bool> cancel { true };
        expect(motion::decodeAsset(*asset, &cancel, &progress).failed());
        expect(asset->source == retained);
        expectEquals(progress.load(), 0.0);
        auto invalid = textAsset(".gpla", json.replace("1,0,0,0,0,1", "1"));
        expect(motion::decodeAsset(invalid).failed());
        std::vector<std::shared_ptr<const motion::PreparedDrawing>> drawings { asset->source->firstFrame(), asset->source->firstFrame() };
        motion::PreparedSource extreme(drawings, std::numeric_limits<double>::denorm_min());
        expectEquals(static_cast<int>(extreme.frameIndex(-1)), 0);

        beginTest("Canonical binary GPLA uses its native frame rate");
        juce::MemoryOutputStream binary;
        binary.write("GPLA    ", 8);
        for (const auto value : { 1, 0, 0 }) {
            binary.writeInt64(value);
        }
        binary.write("FILE    fCount  ", 16);
        binary.writeInt64(2);
        binary.write("fRate   ", 8);
        binary.writeInt64(24);
        binary.write("DONE    ", 8);
        for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
            binary.write("FRAME   focalLen", 16);
            binary.writeDouble(1);
            binary.write("OBJECTS OBJECT  MATRIX  ", 24);
            for (int i = 0; i < 16; ++i) {
                binary.writeDouble(i % 5 == 0 ? 1 : 0);
            }
            binary.write("DONE    STROKES STROKE  vertexCt", 32);
            binary.writeInt64(2);
            binary.write("VERTICES", 8);
            for (const auto x : { 0.1, 0.4 }) {
                binary.writeDouble(x + frameIndex * 0.2);
                binary.writeDouble(0);
                binary.writeDouble(-1);
            }
            for (int i = 0; i < 5; ++i) {
                binary.write("DONE    ", 8);
            }
        }
        binary.write("END GPLA", 8);
        auto binaryAsset = textAsset(".gpla", "");
        binaryAsset.data = binary.getMemoryBlock();
        const auto binaryResult = motion::decodeAsset(binaryAsset);
        expect(binaryResult.wasOk(), binaryResult.getErrorMessage());
        if (binaryResult.wasOk()) {
            expectEquals(binaryAsset.source->frameRate(), 24.0);
            expectEquals(static_cast<int>(binaryAsset.source->frameCount()), 2);
            expect(std::abs(binaryAsset.source->sample(0, 0.3).x - binaryAsset.source->sample(1.5 / 24, 0.3).x) > 0.01f);
        }
        // Frame by frame, the shared parser matches its whole-file result.
        const auto* bytes = static_cast<const char*>(binary.getData());
        int rate = 0;
        const auto whole = LineArtParser::parseBinaryFrames(bytes, static_cast<int>(binary.getDataSize()), rate);
        expect(whole.size() == 2 && rate == 24);
        constexpr int headerBytes = 80, frameBytes = 312;
        for (std::size_t frameIndex = 0; frameIndex < whole.size(); ++frameIndex) {
            const auto single = LineArtParser::parseBinaryFrame(bytes + headerBytes + static_cast<int>(frameIndex) * frameBytes, frameBytes);
            expect(!single.empty() && single.size() == whole[frameIndex].size());
            for (std::size_t line = 0; line < std::min(single.size(), whole[frameIndex].size()); ++line) {
                const auto& a = single[line];
                const auto& b = whole[frameIndex][line];
                expect(a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2);
            }
        }
#if OSCI_PREMIUM
        beginTest("Lottie JSON and dotLottie prepare identical animated geometry");
        const juce::String lottie = R"json({"v":"5.7.4","fr":30,"ip":0,"op":3,"w":100,"h":100,"nm":"Motion test","ddd":0,"assets":[],"layers":[{"ddd":0,"ind":1,"ty":4,"nm":"Line","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":1,"k":[{"t":0,"s":[0,0,0],"e":[20,0,0],"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]}},{"t":2,"s":[20,0,0]}]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]}},"ao":0,"shapes":[{"ty":"sh","nm":"Line","ks":{"a":0,"k":{"i":[[0,0],[0,0]],"o":[[0,0],[0,0]],"v":[[10,50],[50,50]],"c":false}}},{"ty":"st","nm":"Stroke","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100},"w":{"a":0,"k":2},"lc":1,"lj":1,"ml":4,"bm":0}],"ip":0,"op":3,"st":0,"bm":0}]})json";
        auto lottieAsset = textAsset(".json", lottie);
        const auto lottieResult = motion::decodeAsset(lottieAsset);
        expect(lottieResult.wasOk(), lottieResult.getErrorMessage());
        juce::ZipFile::Builder archive;
        archive.addEntry(new juce::MemoryInputStream(lottie.toRawUTF8(), lottie.getNumBytesAsUTF8(), true), 9, "animations/test.json", juce::Time());
        juce::MemoryOutputStream zipped;
        expect(archive.writeToStream(zipped, nullptr));
        auto zippedAsset = textAsset(".lottie", "");
        zippedAsset.data = zipped.getMemoryBlock();
        const auto zippedResult = motion::decodeAsset(zippedAsset);
        expect(zippedResult.wasOk(), zippedResult.getErrorMessage());
        if (lottieResult.wasOk() && zippedResult.wasOk()) {
            expectEquals(static_cast<int>(lottieAsset.source->frameCount()), 3);
            expect(std::abs(lottieAsset.source->sample(0, 0.3).x - lottieAsset.source->sample(2.5 / 30, 0.3).x) > 0.01f);
            expectEquals(lottieAsset.source->sample(2.5 / 30, 0.3).x, zippedAsset.source->sample(2.5 / 30, 0.3).x);
            auto sharedLottie = std::make_shared<motion::Asset>(zippedAsset);
            sharedLottie->id = 5;
            document.edit("Import dotLottie", [&](motion::Project& destination) {
                destination.assets.push_back(sharedLottie);
            });
            const auto archiveReload = restored.load(document.save());
            expect(archiveReload.wasOk(), archiveReload.getErrorMessage());
            if (archiveReload.wasOk()) {
                expectEquals(restored.project().assets.back()->source->sample(2.5 / 30, 0.3).x,
                    zippedAsset.source->sample(2.5 / 30, 0.3).x);
            }

        }
        auto excessive = textAsset(".json", lottie.replace("\"op\":3", "\"op\":3601"));
        expect(motion::decodeAsset(excessive).failed());
#endif
    }

    void testCameras(motion::Document& document) {
        beginTest("Empty camera collections preserve fixed framing and signal sampling");
        const auto sourceProject = document.project();
        motion::PreparedComposition fallback(sourceProject);
        const osci::Point world(0.5f, -0.25f, 2.0f, 0.3f, 0.4f, 0.5f);
        const auto projected = fallback.projectPoint(world, 0);
        expectEquals(projected.x, 1.0f);
        expectEquals(projected.y, -0.5f);
        expectEquals(projected.z, 1.0f);
        expectEquals(projected.r, world.r);
        const auto sampleWorld = fallback.clips.front().sample(0, 0.2);
        const auto sampleOutput = fallback.sample(0, 0.2);
        expectEquals(sampleOutput.x, sampleWorld.x * (4.0f / (4.0f - sampleWorld.z)));
        expectEquals(sampleOutput.y, sampleWorld.y * (4.0f / (4.0f - sampleWorld.z)));

        beginTest("Authored default camera matches fixed framing");
        auto project = sourceProject;
        motion::Camera camera;
        camera.id = document.newId();
        camera.name = "Main camera";
        project.cameras.push_back(camera);
        motion::PreparedComposition authoredDefault(project);
        const auto authored = authoredDefault.projectPoint(world, 0);
        expectWithinAbsoluteError(authored.x, projected.x, 0.000001f);
        expectWithinAbsoluteError(authored.y, projected.y, 0.000001f);

        beginTest("Camera translation and inverse compound rotation recover camera-space points");
        auto& properties = project.cameras[0].properties;
        properties["position.x"] = motion::Curve(1);
        properties["position.y"] = motion::Curve(2);
        properties["position.z"] = motion::Curve(3);
        properties["rotation.x"] = motion::Curve(23);
        properties["rotation.y"] = motion::Curve(-31);
        properties["rotation.z"] = motion::Curve(47);
        motion::PreparedComposition transformed(project);
        const auto toWorld = [](osci::Point point) {
            constexpr auto radians = std::numbers::pi / 180.0;
            point.rotate(23 * radians, -31 * radians, 47 * radians);
            point.translate(1, 2, 3);
            return point;
        };
        const auto rotated = transformed.projectPoint(toWorld({ 0.6f, -0.4f, -4, 1, 0.5f, 0.25f }), 0);
        expectWithinAbsoluteError(rotated.x, 0.6f, 0.00001f);
        expectWithinAbsoluteError(rotated.y, -0.4f, 0.00001f);
        expectEquals(rotated.r, 1.0f);
        expectEquals(rotated.g, 0.5f);
        expectEquals(rotated.b, 0.25f);
        for (const auto depth : { -0.01f, 0.0f, 1.0f }) {
            const auto hidden = transformed.projectPoint(toWorld({ 0, 0, depth, 1, 1, 1 }), 0);
            expectEquals(hidden.r + hidden.g + hidden.b, 0.0f);
            expectEquals(hidden.x + hidden.y + hidden.z, 0.0f);
        }

        beginTest("Camera curves evaluate in project time and field of view controls perspective");
        project.cameras[0] = camera;
        project.cameras[0].properties["position.x"].setKey({ 0, 0, motion::Interpolation::linear });
        project.cameras[0].properties["position.x"].setKey({ 10, 2 });
        motion::PreparedComposition animated(project);
        expectWithinAbsoluteError(animated.projectPoint({ 0, 0, 0 }, 5).x, -1.0f, 0.000001f);
        project.cameras[0] = camera;
        project.cameras[0].properties["fov"].setKey({ 0, motion::defaultCameraFieldOfView, motion::Interpolation::linear });
        project.cameras[0].properties["fov"].setKey({ 10, 90 });
        motion::PreparedComposition wider(project);
        expectWithinAbsoluteError(wider.projectPoint({ 1, 0, 0 }, 10).x, 0.25f, 0.000001f);
        project.cameras[0].properties["fov"].setKey({ 0, 90, motion::Interpolation::cubic, 0, 1000 });
        project.cameras[0].properties["fov"].setKey({ 10, 90, motion::Interpolation::cubic, -1000, 0 });
        motion::PreparedComposition overshoot(project);
        expectEquals(overshoot.projectPoint({ 1, 0, 0, 1, 1, 1 }, 5).r, 0.0f);

        beginTest("Camera cuts are half-open and gaps select the first camera");
        project.cameras[0] = camera;
        auto second = camera;
        second.id = document.newId();
        second.name = "Side camera";
        second.properties["position.x"] = motion::Curve(1);
        second.properties["rotation.z"].setKey({ 0, 0, motion::Interpolation::linear });
        second.properties["rotation.z"].setKey({ 20, 0 });
        project.cameras.push_back(second);
        const auto firstCutId = document.newId();
        const auto secondCutId = document.newId();
        project.cameraCuts = { { firstCutId, second.id, 2, 2 }, { secondCutId, camera.id, 4, 1 } };
        motion::PreparedComposition cuts(project);
        expectEquals(cuts.activeCamera(1.999)->id, camera.id);
        expectEquals(cuts.activeCamera(2)->id, second.id);
        expectEquals(cuts.activeCamera(3.999)->id, second.id);
        expectEquals(cuts.activeCamera(4)->id, camera.id);
        expectEquals(cuts.activeCamera(8)->id, camera.id);
        expectWithinAbsoluteError(cuts.projectPoint({ 0, 0, 0 }, 3).x, -1.0f, 0.000001f);
        expectWithinAbsoluteError(cuts.projectPoint({ 0, 0, 0 }, 4).x, 0.0f, 0.000001f);

        beginTest("Camera edits share imported assets through undo and serialization");
        juce::UndoManager cameraUndo;
        motion::Document cameraDocument(cameraUndo);
        cameraDocument.reset(sourceProject);
        cameraDocument.edit("Add cameras", [&](motion::Project& value) {
            value.cameras = project.cameras;
            value.cameraCuts = project.cameraCuts;
        });
        expect(cameraUndo.undo());
        expect(cameraDocument.project().cameras.empty());
        expect(cameraDocument.project().assets[0] == sourceProject.assets[0]);
        expect(cameraUndo.redo());
        expect(cameraDocument.project().assets[0] == sourceProject.assets[0]);
        const auto xml = cameraDocument.save();
        juce::UndoManager loadedUndo;
        motion::Document loaded(loadedUndo);
        const auto result = loaded.load(xml);
        expect(result.wasOk(), result.getErrorMessage());
        if (result.failed()) {
            return;
        }
        expectEquals(static_cast<int>(loaded.project().cameras.size()), 2);
        expectEquals(static_cast<int>(loaded.project().cameraCuts.size()), 2);
        expectEquals(juce::String(loaded.project().cameras[1].name), juce::String("Side camera"));
        expectEquals(static_cast<int>(loaded.project().cameras[1].properties.at("rotation.z").keyframes().size()), 2);
        expect(loaded.newId() > secondCutId);
        motion::PreparedComposition reloaded(loaded.project());
        expectWithinAbsoluteError(reloaded.projectPoint({ 0, 0, 0 }, 3).x, -1.0f, 0.000001f);

        beginTest("Invalid camera identities, curves, references and overlapping cuts reject atomically");
        const auto unchanged = loaded.save().toString();
        const auto reject = [&](const juce::XmlElement& invalid) {
            expect(loaded.load(invalid).failed());
            expectEquals(loaded.save().toString(), unchanged);
        };
        auto duplicate = std::make_unique<juce::XmlElement>(xml);
        duplicate->getChildByName("camera")->setAttribute("id", juce::String(sourceProject.assets[0]->id));
        reject(*duplicate);
        auto missingCamera = std::make_unique<juce::XmlElement>(xml);
        missingCamera->getChildByName("cameraCut")->setAttribute("camera", "999999");
        reject(*missingCamera);
        auto overlap = std::make_unique<juce::XmlElement>(xml);
        overlap->getChildByName("cameraCut")->setAttribute("duration", 3.0);
        reject(*overlap);
        auto badDuration = std::make_unique<juce::XmlElement>(xml);
        badDuration->getChildByName("cameraCut")->setAttribute("duration", 0.0);
        reject(*badDuration);
        auto duplicateCut = std::make_unique<juce::XmlElement>(xml);
        duplicateCut->getChildByName("cameraCut")->setAttribute("id", juce::String(camera.id));
        reject(*duplicateCut);
        auto badFieldOfView = std::make_unique<juce::XmlElement>(xml);
        for (auto* property : badFieldOfView->getChildByName("camera")->getChildWithTagNameIterator("property")) {
            if (property->getStringAttribute("name") == "fov") {
                property->setAttribute("base", 180.0);
            }
        }
        reject(*badFieldOfView);
    }

};
static MotionDocumentTest motionDocumentTest;


class MotionVideoImportTest : public juce::UnitTest {
public:
    MotionVideoImportTest() : juce::UnitTest("Motion video import", "MotionVideo") {}
    void runTest() override {
        const juce::File fixtures(juce::SystemStats::getEnvironmentVariable("MOTION_VIDEO_FIXTURES", ""));
        const juce::File decoder(juce::SystemStats::getEnvironmentVariable("MOTION_VIDEO_DECODER", ""));
        if (juce::SystemStats::getEnvironmentVariable("MOTION_VIDEO_DECODER", "").isEmpty()) {
            logMessage("Video decoder integration not run: set MOTION_VIDEO_DECODER and MOTION_VIDEO_FIXTURES.");
            return;
        }
        beginTest("Video fixture environment is present");
        expect(fixtures.isDirectory() && decoder.existsAsFile(), "Set MOTION_VIDEO_FIXTURES and MOTION_VIDEO_DECODER; generate fixtures with scripts/generate_motion_video_fixtures.py.");
        if (!fixtures.isDirectory() || !decoder.existsAsFile()) { return; }
        std::shared_ptr<motion::Asset> reference;
        for (const auto* name : {"motion.mp4", "motion.mov", "motion-anamorphic-sar2-1.mp4"}) {
            beginTest(juce::String("Decode, archive and reopen ") + name);
            auto asset = std::make_shared<motion::Asset>();
            asset->id = 1; asset->name = name; asset->extension = fixtures.getChildFile(name).getFileExtension();
            expect(fixtures.getChildFile(name).loadFileAsData(asset->data));
            asset->rasterSettings.resolution = 64; asset->rasterSettings.pointsPerFrame = 1024; asset->rasterSettings.videoFrameRate = 24;
            const auto prepared = motion::decodeAsset(*asset, nullptr, nullptr, decoder);
            expect(prepared.wasOk(), prepared.getErrorMessage());
            if (prepared.failed()) { continue; }
            expectEquals(static_cast<int>(asset->source->frameCount()), 48);
            expectWithinAbsoluteError(asset->source->duration(), 2.0, 1e-12);
            expect(asset->bakedData.getSize() > 0 && asset->bakeKey.isNotEmpty());
            double earlyRed = 0, earlyBlue = 0, lateRed = 0, lateBlue = 0, maxY = 0;
            for (int index = 0; index < 1024; ++index) {
                const auto early = asset->source->sample(.25, (index + .5) / 1024);
                const auto late = asset->source->sample(1.75, (index + .5) / 1024);
                earlyRed += early.r; earlyBlue += early.b; lateRed += late.r; lateBlue += late.b;
                if (early.r + early.g + early.b > .1) { maxY = std::max(maxY, static_cast<double>(std::abs(early.y))); }
            }
            expect(earlyRed > earlyBlue, "First half retains red marks");
            expect(lateBlue > lateRed, "Second half retains blue marks");
            if (juce::String(name).contains("anamorphic")) { expect(maxY < .22, "Anamorphic display aspect halves the vertical beam extent"); }
            else { expect(maxY > .25 && maxY < .4, "Square-pixel source preserves its expected vertical extent"); }
            motion::Project project; project.duration = 2; project.assets.push_back(asset);
            motion::Track track; track.id = 2; track.clips.push_back(motion::Document::makeClip(3, *asset, 0)); project.tracks.push_back(track);
            juce::UndoManager undo; motion::Document document(undo); document.reset(project);
            juce::UndoManager loadedUndo; motion::Document loaded(loadedUndo);
            const auto restored = loaded.load(document.save());
            expect(restored.wasOk(), restored.getErrorMessage());
            if (restored.wasOk()) {
                const auto reopened = loaded.project().assets.front()->source;
                for (const auto time : {1.8, .02, .999, 1.001, .35}) {
                    for (int index = 0; index < 50; ++index) {
                        const auto phase = index / 50.0;
                        const auto a = asset->source->sample(time, phase), b = reopened->sample(time, phase);
                        expectEquals(a.x, b.x); expectEquals(a.y, b.y); expectEquals(a.r, b.r); expectEquals(a.b, b.b);
                    }
                }
            }
            auto invalid = document.save();
            invalid.getChildByName("asset")->getChildByName("raster")->setAttribute("threshold", .9);
            const auto before = loaded.save().toString();
            expect(loaded.load(invalid).failed()); expectEquals(loaded.save().toString(), before);
            invalid = document.save();
            invalid.getChildByName("asset")->removeChildElement(invalid.getChildByName("asset")->getChildByName("video-cache"), true);
            expect(loaded.load(invalid).failed()); expectEquals(loaded.save().toString(), before);
            if (juce::String(name) == "motion.mp4") {
                reference = asset;
                juce::TemporaryFile exported(".wav"); std::atomic<bool> cancel {false};
                const auto result = motion::SignalExporter::write(project, exported.getFile(), 48000, cancel);
                expect(result.wasOk(), result.getErrorMessage());
                if (result.wasOk()) {
                    juce::WavAudioFormat format;
                    auto stream = exported.getFile().createInputStream();
                    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
                    expect(reader != nullptr);
                    if (reader) {
                        expectEquals(reader->lengthInSamples, static_cast<juce::int64>(96000));
                        juce::AudioBuffer<float> signal(5, static_cast<int>(reader->lengthInSamples));
                        expect(reader->read(signal.getArrayOfWritePointers(), 5, 0, signal.getNumSamples()));
                        motion::PreparedComposition exact(project, 48000);
                        for (int index = 0; index < signal.getNumSamples(); index += 421) {
                            const auto point = exact.sample(index / 48000.0, std::fmod(index * 60.0 / 48000, 1.0), 60.0 / 48000, 1.0 / 48000);
                            expectEquals(signal.getSample(0, index), point.x); expectEquals(signal.getSample(1, index), point.y);
                            expectEquals(signal.getSample(2, index), point.r); expectEquals(signal.getSample(4, index), point.b);
                        }
                    }
                }
            }
        }
        if (reference == nullptr) { return; }
        beginTest("A one-frame video retains its authored clip duration");
        motion::Asset single; single.id = 4; single.name = "single-frame.mp4"; single.extension = ".mp4";
        single.rasterSettings = reference->rasterSettings;
        expect(fixtures.getChildFile("single-frame.mp4").loadFileAsData(single.data));
        const auto singleResult = motion::decodeAsset(single, nullptr, nullptr, decoder);
        expect(singleResult.wasOk(), singleResult.getErrorMessage());
        if (singleResult.wasOk()) { expectWithinAbsoluteError(motion::Document::makeClip(5, single, 0).duration, 1.0 / 24, 1e-12); }
        beginTest("Corrupt media, absent decoder, bounded output and cancellation reject cleanly");
        juce::MemoryBlock corrupt; expect(fixtures.getChildFile("corrupt.mp4").loadFileAsData(corrupt));
        const auto corruptResult = motion::VideoSourcePreparer::prepare(corrupt, decoder, reference->rasterSettings);
        expect(!corruptResult && juce::String(corruptResult.error).contains("Cannot decode"), juce::String(corruptResult.error));
        expect(!motion::VideoSourcePreparer::prepare(reference->data, {}, reference->rasterSettings));
#if JUCE_MAC || JUCE_LINUX
        juce::TemporaryFile failingDecoder(".sh");
        expect(failingDecoder.getFile().replaceWithText("#!/bin/sh\nfor last; do :; done\nhead -c 16384 /dev/zero > \"$last\"\nexit 1\n", false, false, "\n"));
        expect(failingDecoder.getFile().setExecutePermission(true));
        const auto partialFailure = motion::VideoSourcePreparer::prepare(reference->data, failingDecoder.getFile(), reference->rasterSettings);
        expect(!partialFailure && juce::String(partialFailure.error).contains("Cannot decode"), "A failing decoder must not publish its complete but partial output: " + juce::String(partialFailure.error));
        expect(failingDecoder.getFile().replaceWithText("#!/bin/sh\nfor last; do :; done\nhead -c 16384 /dev/zero > \"$last\"\nkill -KILL $$\n", false, false, "\n"));
        expect(failingDecoder.getFile().setExecutePermission(true));
        const auto crashed = motion::VideoSourcePreparer::prepare(reference->data, failingDecoder.getFile(), reference->rasterSettings);
        expect(!crashed && juce::String(crashed.error).contains("Cannot decode"), "A crashed decoder must not publish partial output: " + juce::String(crashed.error));
#endif

        motion::VideoSourcePreparer::Limits limits; limits.rawBytes = 64 * 64 * 4 * 4;
        const auto bounded = motion::VideoSourcePreparer::prepare(reference->data, decoder, reference->rasterSettings, nullptr, nullptr, limits);
        expect(!bounded && juce::String(bounded.error).contains("budget"));
        std::atomic<bool> cancel {true};
        expect(!motion::VideoSourcePreparer::prepare(reference->data, decoder, reference->rasterSettings, &cancel));
        cancel.store(false);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        std::thread canceller([&] { juce::Thread::sleep(20); cancel.store(true); });
        const auto cancelled = motion::VideoSourcePreparer::prepare(reference->data, decoder, reference->rasterSettings, &cancel);
        canceller.join();
        expect(!cancelled && juce::String(cancelled.error).contains("cancelled"));
        expect(juce::Time::getMillisecondCounterHiRes() - start < 2000, "Decoder cancellation returns promptly");
    }
};
static MotionVideoImportTest motionVideoImportTest;
