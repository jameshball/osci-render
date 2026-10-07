#include <JuceHeader.h>
#include "../Source/motion/render/BeamRenderer.h"
#include "../Source/motion/live/LiveSourceExchange.h"
#include "../Source/motion/live/PreparedBlenderFrame.h"
#include "../Source/motion/export/SignalExporter.h"
#include <thread>
#include <future>

class MotionLiveSourceTest : public juce::UnitTest {
public:
    MotionLiveSourceTest() : juce::UnitTest("Motion immutable live source rendering", "MotionLive") {}
    void runTest() override {
        const auto identity = std::make_shared<const motion::LiveSourceIdentity>();
        auto project = makeProject(identity);
        motion::PreparedComposition composition(project);
        beginTest("Live leaves awaiting their first frame retain their timeline allocation and draw darkness");
        expect(composition.preparationError.isEmpty(), composition.preparationError);
        expectEquals(static_cast<int>(composition.clips.size()), 2);
        const auto beamAt = [&](std::int64_t index, const motion::LiveSourceFrames* live) {
            motion::BeamRenderer beam;
            return beam.sample(composition, .25, index, 48000, false, 1, live);
        };
        expect(dark(beamAt(123, nullptr)));
        auto one = frames(identity, 1);
        auto two = frames(identity, 2);
        beginTest("All instances resolve the same source while retaining their independent transforms");
        expectWithinAbsoluteError(composition.clips[0].sample(.25, .3, 0, 0, one.get()).x, 1.0f, 1e-6f);
        expectWithinAbsoluteError(composition.clips[1].sample(.25, .3, 0, 0, one.get()).x, 4.0f, 1e-6f);
        expectWithinAbsoluteError(composition.clips[0].sample(.25, .3, 0, 0, two.get()).x, 2.0f, 1e-6f);
        expectWithinAbsoluteError(composition.clips[1].sample(.25, .3, 0, 0, two.get()).x, 5.0f, 1e-6f);
        beginTest("Beam plans cannot retain fallback geometry for a live leaf");
        for (int index = 1; index < 800; ++index) {
            const auto point = beamAt(index, two.get());
            if (!dark(point)) { expect(point.x == 2 || point.x == 5); }
        }
        beginTest("Unrelated source identities and explicit empty frames cannot reuse earlier geometry");
        auto foreign = frames(std::make_shared<const motion::LiveSourceIdentity>(), 9);
        expect(dark(beamAt(123, foreign.get())));
        motion::BlenderFrame blank;
        blank.frameRate = 24;
        auto empty = std::make_shared<const motion::LiveSourceFrames>(std::vector<motion::LiveSourceFrames::Entry>{{identity, motion::prepareBlenderFrame(blank)}});
        expect(dark(beamAt(123, empty.get())));
        auto disconnected = std::make_shared<const motion::LiveSourceFrames>(std::vector<motion::LiveSourceFrames::Entry>{{identity, nullptr}});
        expect(dark(beamAt(123, disconnected.get())));
        beginTest("Uncaptured live sources reject offline signal export before creating output");
        juce::TemporaryFile destination(".wav");
        const std::atomic<bool> cancel{false};
        const auto result = motion::SignalExporter::write(composition, destination.getFile(), 48000, cancel);
        expect(result.failed() && result.getErrorMessage().contains("Capture live sources"));
        expect(!destination.getFile().existsAsFile());
        beginTest("One borrowed set stays stable through publisher churn until the next block");
        motion::LiveSourceExchange exchange;
        exchange.publish(one);
        const auto* held = exchange.acquire();
        const auto revision = held->revision;
        const auto* heldSource = held->frames->resolve(identity.get());
        for (int index = 0; index < 1000; ++index) { exchange.publish(two); }
        expect(held->revision == revision && held->frames->resolve(identity.get()) == heldSource);
        expectWithinAbsoluteError(heldSource->sample(0, .3).x, 1.0f, 1e-6f);
        expect(exchange.previewSnapshot() == two);
        const auto* next = exchange.acquire();
        expect(next->revision > revision && next->frames == two);
        exchange.collect();
        beginTest("Concurrent publication and block borrowing retain immutable geometry");
        std::atomic<bool> done{false}, consistent{true};
        std::promise<void> started;
        auto ready = started.get_future();
        std::atomic<int> blocks{0};
        std::thread render([&] {
            bool first = true;
            while (!done.load()) {
                const auto* block = exchange.acquire();
                if (block == nullptr || block->frames == nullptr) { continue; }
                const auto* source = block->frames->resolve(identity.get());
                if (first) { started.set_value(); first = false; }
                ++blocks;
                const auto x = source->sample(0, .2).x;
                for (int sample = 0; sample < 256; ++sample) {
                    if (source->sample(0, sample / 256.0).x != x) { consistent.store(false); }
                }
            }
        });
        ready.wait();
        for (int index = 0; index < 20000; ++index) { exchange.publish(index % 2 == 0 ? one : two); }
        done.store(true);
        render.join();
        exchange.collect();
        expect(consistent.load() && blocks.load() > 0);
        beginTest("Duplicate runtime identities are rejected before publication");
        bool rejected = false;
        try { motion::LiveSourceFrames duplicate({{identity, nullptr}, {identity, nullptr}}); }
        catch (const std::invalid_argument&) { rejected = true; }
        expect(rejected);
    }
private:
    static bool dark(const osci::Point& point) { return point.r == 0 && point.g == 0 && point.b == 0; }
    static std::shared_ptr<const motion::LiveSourceFrames> frames(std::shared_ptr<const motion::LiveSourceIdentity> identity, double x) {
        motion::BlenderFrame frame;
        frame.frameRate = 24;
        frame.segments.push_back({x, 0, x, 1});
        return std::make_shared<const motion::LiveSourceFrames>(std::vector<motion::LiveSourceFrames::Entry>{{std::move(identity), motion::prepareBlenderFrame(frame)}});
    }
    static motion::Project makeProject(std::shared_ptr<const motion::LiveSourceIdentity> identity) {
        motion::Project project;
        project.duration = 1;
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "Live Blender";
        asset->liveIdentity = std::move(identity);
        motion::BlenderFrame fallback;
        fallback.frameRate = 24;
        fallback.segments.push_back({-99, 0, -99, 1});
        asset->source = motion::prepareBlenderFrame(fallback);
        project.assets.push_back(asset);
        for (int index = 0; index < 2; ++index) {
            motion::Track track;
            track.id = 2 + index * 2;
            motion::Clip clip;
            clip.id = track.id + 1;
            clip.asset = asset->id;
            clip.duration = 1;
            clip.properties["position.x"] = motion::Curve(index * 3);
            track.clips.push_back(clip);
            project.tracks.push_back(track);
        }
        return project;
    }
};
static MotionLiveSourceTest motionLiveSourceTest;
