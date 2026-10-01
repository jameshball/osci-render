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

        beginTest("Routed modulators and links reach slider bakes and their key");
        {
            document.edit("Route an LFO", [&](motion::Project& updated) {
                updated.tracks[0].clips[0].properties["slider.a"].base = 0.25;
                motion::Modulator lfo;
                lfo.id = 7001; lfo.name = "LFO"; lfo.shape.waveform = motion::ModulationWaveform::square; lfo.shape.rateHz = 1;
                updated.modulators.push_back(lfo);
                motion::ModulationRoute route;
                route.id = 7002; route.modulator = lfo.id; route.target = updated.tracks[0].clips[0].id; route.property = "slider.a"; route.amount = 0.5;
                updated.routes.push_back(route);
            });
            const auto& routed = document.project().tracks[0].clips[0];
            const auto plain = motion::LuaSliderBakes::planFor(*asset, routed, motion::Tempo(120));
            const auto driven = motion::LuaSliderBakes::planFor(*asset, routed, document.project());
            expect(plain.has_value() && driven.has_value() && plain->key != driven->key, "a route changes what the bake must contain");
            if (driven.has_value()) {
                // A square LFO swings the slider by the route's amount around its base.
                const auto& slider = driven->sliders.at("slider.a");
                expect(std::abs(slider.evaluate(0.1, 120) - slider.evaluate(0.6, 120)) > 0.5, "the routed LFO moves the slider");
            }
            document.edit("Deepen route", [](motion::Project& updated) { updated.routes[0].amount = 0.25; });
            const auto deeper = motion::LuaSliderBakes::planFor(*asset, document.project().tracks[0].clips[0], document.project());
            expect(deeper.has_value() && driven.has_value() && deeper->key != driven->key, "editing the route re-bakes");
            motion::Project reopened;
            expect(motion::Document::prepareLoad(document.save(), reopened).wasOk());
            const auto again = motion::LuaSliderBakes::planFor(*reopened.assets[0], reopened.tracks[0].clips[0], reopened);
            expect(again.has_value() && deeper.has_value() && again->key == deeper->key, "a saved routed bake still matches after loading");
            document.edit("Move clip", [](motion::Project& updated) { updated.tracks[0].clips[0].start = 1; });
            const auto moved = motion::LuaSliderBakes::planFor(*asset, document.project().tracks[0].clips[0], document.project());
            expect(moved.has_value() && deeper.has_value() && moved->key != deeper->key, "moving a routed clip re-bakes");
            expect(document.changeTempo(90).wasOk());
            const auto retimed = motion::LuaSliderBakes::planFor(*asset, document.project().tracks[0].clips[0], document.project());
            expect(retimed.has_value() && moved.has_value() && retimed->key != moved->key, "a tempo change re-bakes routed sliders");
        }
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
