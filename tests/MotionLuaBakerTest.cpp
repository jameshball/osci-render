#include <JuceHeader.h>
#include "../Source/motion/import/LuaBaker.h"
#include "../Source/motion/model/PreparedSource.h"
#include "../Source/motion/model/Document.h"

class MotionLuaBakerTest : public juce::UnitTest {
public:
    MotionLuaBakerTest() : juce::UnitTest("Motion Lua baking", "MotionBake") {}
    void runTest() override {
        motion::BakeSettings settings;
        settings.duration = 0.1;
        settings.frameRate = 20;
        settings.pointsPerFrame = 16;
        beginTest("Stateful Lua is evaluated once in source order and supports independent seeks");
        const auto baked = motion::LuaBaker::bake("counter.lua", "n=(n or 0)+1; return {n,play_time}", settings);
        expect(static_cast<bool>(baked), baked.error);
        if (!baked) { return; }
        motion::PreparedSource source(baked.source);
        expectEquals(static_cast<int>(source.frameCount()), 2);
        expectWithinAbsoluteError(source.duration(), 0.1, 1.0e-12);
        expectEquals(source.sample(0.05, 0).x, 17.0f);
        expectEquals(source.sample(0, 0).x, 1.0f);
        expectEquals(source.sample(0.05, 0).x, 17.0f);
        expectEquals(source.sample(0, 0).r, -1.0f);
        expect(!source.hasExplicitColour());
        const auto inherited = motion::LuaBaker::bake("inherited.lua", "return {1,2,3,-1,-1,-1}", settings);
        expect(static_cast<bool>(inherited), inherited.error);
        if (inherited) { expect(!inherited.source->hasExplicitColour()); }

        beginTest("Baked color travels through PreparedSource without changing sample density");
        const auto colored = motion::LuaBaker::bake("rgb.lua", "return {step,0,0,0.25,0.5,1}", settings);
        expect(static_cast<bool>(colored), colored.error);
        if (colored) {
            motion::PreparedSource rgb(colored.source);
            expect(rgb.hasExplicitColour());
            const auto sample = rgb.sample(0, 0.5 / 16);
            expectEquals(sample.x, 1.5f);
            expectEquals(sample.r, 0.25f);
            expectEquals(sample.g, 0.5f);
            expectEquals(sample.b, 1.0f);
        }

        beginTest("Fresh bakes reproduce seeded random values and report completion");
        std::atomic<double> progress {-1};
        const auto a = motion::LuaBaker::bake("random.lua", "return {math.random(),math.random()}", settings, nullptr, &progress);
        const auto b = motion::LuaBaker::bake("random.lua", "return {math.random(),math.random()}", settings);
        expect(static_cast<bool>(a) && static_cast<bool>(b));
        expectEquals(progress.load(), 1.0);
        if (a && b) {
            for (std::size_t i = 0; i < a.source->data().size(); ++i) {
                expectEquals(a.source->data()[i].x, b.source->data()[i].x);
                expectEquals(a.source->data()[i].y, b.source->data()[i].y);
            }
        }

        beginTest("Invalid settings, malformed signals, script errors and cancellation produce no source");
        for (const auto* script : {"return {1}", "return {0/0,1}", "return {1,2,3,4,5,6}", "while true do end", "error('no')"}) {
            const auto invalid = motion::LuaBaker::bake("invalid.lua", script, settings);
            expect(!invalid && !invalid.error.empty());
        }
        std::atomic<bool> cancelled {true};
        expect(!motion::LuaBaker::bake("cancel.lua", "return {1,2}", settings, &cancelled));
        settings.duration = 1.0e300;
        expect(!motion::LuaBaker::bake("huge.lua", "return {1,2}", settings));

        beginTest("Lua assets embed prepared signals and reject missing or stale caches on reopen");
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "cached.lua";
        asset->extension = ".lua";
        asset->bakeSettings.duration = 0.1;
        asset->bakeSettings.frameRate = 20;
        asset->bakeSettings.pointsPerFrame = 16;
        const juce::String script("return {step,0,0,1,0.5,0.25}");
        asset->data.append(script.toRawUTF8(), script.getNumBytesAsUTF8());
        const auto decoded = motion::Document::decodeAsset(*asset);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        if (decoded.failed()) { return; }
        expect(asset->bakedData.getSize() > 0);
        const auto clip = motion::Document::makeClip(2, *asset, 0);
        expectEquals(clip.properties.at("red").evaluate(0), 1.0);
        expectEquals(clip.properties.at("blue").evaluate(0), 1.0);
        juce::UndoManager undo;
        motion::Document document(undo);
        motion::Project project;
        project.assets.push_back(asset);
        document.reset(project);
        const auto xml = document.save();
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto loaded = restored.load(xml);
        expect(loaded.wasOk(), loaded.getErrorMessage());
        if (loaded.wasOk()) {
            expectEquals(restored.project().assets.front()->source->sample(0.05, 0).x, 17.0f);
            expectEquals(restored.project().assets.front()->bakeKey, asset->bakeKey);
        }
        auto stale = xml;
        stale.getChildByName("asset")->getChildByName("bake")->setAttribute("bpm", 130);
        expect(restored.load(stale).failed());
        auto missing = xml;
        missing.getChildByName("asset")->deleteAllChildElementsWithTagName("bake");
        expect(restored.load(missing).failed());
        expectEquals(static_cast<int>(restored.project().assets.size()), 1);

        beginTest("Fractional bake FPS and tempo survive XML text serialization exactly");
        asset->bakeSettings.frameRate = 24000.0 / 1001.0;
        asset->bakeSettings.bpm = 120.12345678901234;
        asset->bakedData.reset();
        expect(motion::Document::decodeAsset(*asset).wasOk());
        const auto textual = juce::parseXML(document.save().toString());
        expect(textual != nullptr);
        if (textual != nullptr) {
            const auto reopened = restored.load(*textual);
            expect(reopened.wasOk(), reopened.getErrorMessage());
            if (reopened.wasOk()) {
                expectEquals(restored.project().assets.front()->bakeSettings.frameRate, asset->bakeSettings.frameRate);
                expectEquals(restored.project().assets.front()->bakeSettings.bpm, asset->bakeSettings.bpm);
            }
        }
    }
};
static MotionLuaBakerTest motionLuaBakerTest;
