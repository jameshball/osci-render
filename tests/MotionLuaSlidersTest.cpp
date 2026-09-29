#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/LuaSliderBakes.h"
#include "../Source/motion/render/CompositionRenderer.h"

class MotionLuaSlidersTest : public juce::UnitTest {
public:
    MotionLuaSlidersTest() : juce::UnitTest("Motion Lua sliders", "MotionBake") {}

    void runTest() override {
        juce::MessageManager::getInstance();
        beginTest("Clips without slider curves need no bake");
        juce::UndoManager undo;
        motion::Document document(undo);
        auto asset = std::make_shared<motion::Asset>();
        asset->id = document.newId(); asset->name = "radius.lua"; asset->extension = ".lua";
        const juce::String script("return {slider_a * math.cos(phase), slider_a * math.sin(phase)}");
        asset->data.append(script.toRawUTF8(), script.getNumBytesAsUTF8());
        asset->bakeSettings.duration = 1; asset->bakeSettings.frameRate = 10; asset->bakeSettings.pointsPerFrame = 32;
        const auto decoded = motion::Document::decodeAsset(*asset);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        auto clip = motion::Document::makeClip(document.newId(), *asset, 0);
        clip.duration = 2;
        motion::Track track;
        track.id = document.newId(); track.name = "Lua"; track.insert(clip, motion::Tempo(120));
        motion::Project project;
        project.duration = 4; project.assets = {asset}; project.tracks = {track};
        document.reset(project);

        expect(!motion::LuaSliderBakes::planFor(*asset, clip, motion::Tempo(120)).has_value());

        beginTest("Animated sliders bake per clip over its content and install as a cache");
        document.edit("Animate slider", [&](motion::Project& updated) {
            auto& curve = updated.tracks[0].clips[0].properties["slider.a"];
            curve.setKey({0, 0, motion::Interpolation::linear});
            curve.setKey({2, 1, motion::Interpolation::linear});
        });
        const auto& animated = document.project().tracks[0].clips[0];
        const auto plan = motion::LuaSliderBakes::planFor(*asset, animated, motion::Tempo(120));
        expect(plan.has_value() && plan->settings.duration == 2.0, "the bake covers the clip's two seconds of content");
        motion::LuaSliderBakes bakes(document);
        bakes.update();
        for (int attempt = 0; attempt < 400 && document.project().tracks[0].clips[0].luaBake == nullptr; ++attempt) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        const auto& baked = document.project().tracks[0].clips[0];
        expect(baked.luaBake != nullptr && baked.luaBake->key == plan->key);
        expect(undo.getUndoDescription() == "Animate slider", "installing a bake adds no undo step");
        if (baked.luaBake != nullptr) {
            const auto& source = *baked.luaBake->source;
            expectEquals(static_cast<int>(source.frameCount()), 20);
            // Radius follows the slider: 0 at the start, about 0.95 in the last frame.
            const auto radius = [&](double seconds) { const auto point = source.sample(seconds, 0); return std::hypot(point.x, point.y); };
            expectWithinAbsoluteError(radius(0), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(radius(1.95), 0.95f, 1.0e-4f);
            motion::PreparedComposition composition(document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            expect(composition.clips.size() == 1 && composition.clips[0].source == baked.luaBake->source, "the renderer plays the clip's own bake");
        }

        beginTest("Installing a bake is not an edit; undo keeps the bake that still matches");
        {
            const auto revision = document.revision();
            const auto current = document.project().tracks[0].clips[0].luaBake;
            expect(current != nullptr);
            document.setLuaBake(document.project().tracks[0].clips[0].id, current);
            expectEquals(static_cast<int>(document.revision()), static_cast<int>(revision));
            document.edit("Unrelated", [](motion::Project& updated) { updated.duration = 5; });
            expect(undo.undo());
            expect(document.project().tracks[0].clips[0].luaBake == current, "undo carries the installed bake forward");
        }
        beginTest("Bakes save with the project and load without running the script");
        motion::Project loaded;
        const auto result = motion::Document::prepareLoad(document.save(), loaded);
        expect(result.wasOk(), result.getErrorMessage());
        expect(loaded.tracks[0].clips[0].luaBake != nullptr && loaded.tracks[0].clips[0].luaBake->key == plan->key);

        beginTest("A stale saved bake is dropped at load");
        {
            auto xml = document.save();
            for (auto* property : xml.getChildByName("track")->getChildByName("clip")->getChildWithTagNameIterator("property")) {
                if (property->getStringAttribute("name") == "slider.a") { property->getChildByName("key")->setAttribute("value", "0.25"); }
            }
            motion::Project stale;
            expect(motion::Document::prepareLoad(xml, stale).wasOk());
            expect(stale.tracks[0].clips[0].luaBake == nullptr);
        }
        beginTest("Editing a slider makes the bake stale; removing the sliders clears it");
        document.edit("Change slider", [&](motion::Project& updated) { updated.tracks[0].clips[0].properties["slider.a"].setKeyValue(2, 0.5); });
        const auto changed = motion::LuaSliderBakes::planFor(*asset, document.project().tracks[0].clips[0], motion::Tempo(120));
        expect(changed.has_value() && changed->key != plan->key);
        document.edit("Remove slider", [&](motion::Project& updated) { updated.tracks[0].clips[0].properties.erase("slider.a"); });
        bakes.update();
        expect(document.project().tracks[0].clips[0].luaBake == nullptr);

        beginTest("Replacing a Lua source with another kind removes its sliders");
        document.edit("Slider again", [&](motion::Project& updated) { updated.tracks[0].clips[0].properties["slider.b"].base = 0.5; });
        auto shape = std::make_shared<motion::Asset>();
        shape->id = asset->id; shape->name = "tri.obj"; shape->extension = ".obj";
        const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        shape->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
        expect(motion::Document::decodeAsset(*shape).wasOk());
        expect(document.replaceAsset(asset->id, shape).wasOk());
        expect(!document.project().tracks[0].clips[0].properties.contains("slider.b"));
        motion::Project reloaded;
        expect(motion::Document::prepareLoad(document.save(), reloaded).wasOk(), "the project still loads");

        beginTest("Text animations share settled frames and refuse over-long timing");
        {
            auto text = std::make_shared<motion::Asset>();
            text->id = 900; text->name = "hi.txt"; text->extension = ".txt";
            const juce::String hello("HELLO");
            text->data.append(hello.toRawUTF8(), hello.getNumBytesAsUTF8());
            text->textSettings.animation = motion::TextSettings::Animation::rise;
            text->textSettings.characterDelay = 0.1; text->textSettings.characterDuration = 0.2; text->textSettings.hold = 1;
            expect(motion::Document::decodeAsset(*text).wasOk());
            const auto& source = *text->source;
            expectEquals(static_cast<int>(source.frameCount()), 48); // (0.4 + 0.2 + 1) s at 30 fps
            expect(source.drawingAt(40) == source.drawingAt(47), "hold frames share one drawing");
            expect(source.drawingAt(0) != source.drawingAt(10));
            text->textSettings.hold = 30; text->textSettings.characterDelay = 30;
            expect(motion::Document::decodeAsset(*text).failed(), "over 120 seconds is refused, not truncated");
        }
    }
};

static MotionLuaSlidersTest motionLuaSlidersTest;
