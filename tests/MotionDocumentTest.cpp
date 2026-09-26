#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/CompositionRenderer.h"
#include "../Source/motion/model/PropertyTarget.h"

class MotionDocumentTest : public juce::UnitTest {
public:
    MotionDocumentTest() : juce::UnitTest("Motion document and signal", "Motion") {}
    void runTest() override {
        juce::UndoManager undo;
        motion::Document document(undo);
        auto asset = std::make_shared<motion::Asset>();
        asset->id = document.newId();
        asset->name = "triangle.obj";
        asset->extension = ".obj";
        const juce::String source("v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n");
        asset->data.append(source.toRawUTF8(), source.getNumBytesAsUTF8());
        beginTest("Imported assets and independent clip curves survive a project round trip");
        const auto decoded = motion::Document::decodeAsset(*asset);
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
        expect(track.insert(clip));
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
        auto& properties = project.tracks[0].clips[0].properties;
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
        testTrackStates(document.project());
    }

private:
    void testTrackStates(const motion::Project& sourceProject) {
        beginTest("Mute and solo exclude tracks from signal and drawing allocation");
        auto project = sourceProject;
        auto& original = project.tracks[0].clips[0];
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
        project.tracks[1].muted = true;
        const motion::PreparedComposition muted(project);
        expectEquals(static_cast<int>(muted.clips.size()), 1);
        expectEquals(muted.sample(1, 0.75).r, 1.0f);
        expectEquals(muted.sample(1, 0.75).g, 0.0f);
        expectEquals(both.sample(1, 0.75).g, 1.0f);

        project.tracks[1].muted = false;
        project.tracks[1].solo = true;
        const motion::PreparedComposition solo(project);
        expectEquals(static_cast<int>(solo.clips.size()), 1);
        expectEquals(solo.sample(1, 0.25).g, 1.0f);
        project.tracks[1].muted = true;
        const motion::PreparedComposition mutedSolo(project);
        expect(mutedSolo.clips.empty());
        const auto dark = mutedSolo.sample(1, 0.25);
        expectEquals(dark.r + dark.g + dark.b, 0.0f);
        project.tracks[0].solo = true;
        const motion::PreparedComposition multipleSolo(project);
        expectEquals(static_cast<int>(multipleSolo.clips.size()), 1);
        expectEquals(multipleSolo.sample(1, 0.75).r, 1.0f);

        beginTest("Lock leaves playback unchanged and excluded tracks consume no fade budget");
        project.tracks[0].locked = true;
        project.tracks[0].clips[0].properties["weight"] = motion::Curve(0.25);
        const motion::PreparedComposition locked(project);
        int litSamples = 0;
        for (int index = 0; index < 1000; ++index) {
            litSamples += locked.sample(1, index / 1000.0).r > 0 ? 1 : 0;
        }
        expectEquals(litSamples, 250);
        project.tracks[1].muted = false;
        const motion::PreparedComposition bothSolo(project);
        expectEquals(static_cast<int>(bothSolo.clips.size()), 2);

        beginTest("Track state survives save, load, undo and redo without copying assets");
        project.tracks[1].muted = true;
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
        auto& clip = project.tracks[0].clips[0];
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
        project.tracks[0].effects = { scale };
        auto global = motion::makeEffect(105, *motion::effectDefinition("translate"));
        global.properties["translateX"] = motion::Curve(0.1);
        project.effects = { global };
        motion::PreparedComposition composition(project);
        const auto local = clip.localTime(3);
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
        const auto result = motion::Document::decodeAsset(*asset, nullptr, &progress);
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
        expect(independent.stretch(4));
        project.tracks[0].clips.push_back(independent);
        motion::PreparedComposition instances(project);
        expect(instances.clips[0].source == instances.clips[1].source);
        expectEquals(instances.clips[1].sample(3, 0.3).x, first.x);
        const auto beforeTrim = instances.clips[1].sample(3.05, 0.3).x;
        expect(project.tracks[0].clips[1].trim(3.05, 7));
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
        expect(motion::Document::decodeAsset(blank).wasOk());
        if (blank.source != nullptr) {
            const auto dark = blank.source->sample(1.5 / 30.0, 0.3);
            expectEquals(dark.r + dark.g + dark.b, 0.0f);
        }
        const auto retained = asset->source;
        std::atomic<bool> cancel { true };
        expect(motion::Document::decodeAsset(*asset, &cancel, &progress).failed());
        expect(asset->source == retained);
        expectEquals(progress.load(), 0.0);
        auto invalid = textAsset(".gpla", json.replace("1,0,0,0,0,1", "1"));
        expect(motion::Document::decodeAsset(invalid).failed());
        std::vector<std::shared_ptr<const osci::PreparedDrawing>> drawings { asset->drawing, asset->drawing };
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
        const auto binaryResult = motion::Document::decodeAsset(binaryAsset);
        expect(binaryResult.wasOk(), binaryResult.getErrorMessage());
        if (binaryResult.wasOk()) {
            expectEquals(binaryAsset.source->frameRate(), 24.0);
            expectEquals(static_cast<int>(binaryAsset.source->frameCount()), 2);
            expect(std::abs(binaryAsset.source->sample(0, 0.3).x - binaryAsset.source->sample(1.5 / 24, 0.3).x) > 0.01f);
        }
#if OSCI_PREMIUM
        beginTest("Lottie JSON and dotLottie prepare identical animated geometry");
        const juce::String lottie = R"json({"v":"5.7.4","fr":30,"ip":0,"op":3,"w":100,"h":100,"nm":"Motion test","ddd":0,"assets":[],"layers":[{"ddd":0,"ind":1,"ty":4,"nm":"Line","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":1,"k":[{"t":0,"s":[0,0,0],"e":[20,0,0],"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]}},{"t":2,"s":[20,0,0]}]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]}},"ao":0,"shapes":[{"ty":"sh","nm":"Line","ks":{"a":0,"k":{"i":[[0,0],[0,0]],"o":[[0,0],[0,0]],"v":[[10,50],[50,50]],"c":false}}},{"ty":"st","nm":"Stroke","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100},"w":{"a":0,"k":2},"lc":1,"lj":1,"ml":4,"bm":0}],"ip":0,"op":3,"st":0,"bm":0}]})json";
        auto lottieAsset = textAsset(".json", lottie);
        const auto lottieResult = motion::Document::decodeAsset(lottieAsset);
        expect(lottieResult.wasOk(), lottieResult.getErrorMessage());
        juce::ZipFile::Builder archive;
        archive.addEntry(new juce::MemoryInputStream(lottie.toRawUTF8(), lottie.getNumBytesAsUTF8(), true), 9, "animations/test.json", juce::Time());
        juce::MemoryOutputStream zipped;
        expect(archive.writeToStream(zipped, nullptr));
        auto zippedAsset = textAsset(".lottie", "");
        zippedAsset.data = zipped.getMemoryBlock();
        const auto zippedResult = motion::Document::decodeAsset(zippedAsset);
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
        expect(motion::Document::decodeAsset(excessive).failed());
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
