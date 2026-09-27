#include <JuceHeader.h>
#include "../Source/motion/live/LiveBlenderController.h"
#include <bit>

class MotionBlenderLifecycleTest : public juce::UnitTest {
public:
    MotionBlenderLifecycleTest() : juce::UnitTest("Motion Blender asset lifecycle", "MotionLive") {}
    void runTest() override {
        juce::UndoManager undo;
        motion::Document document(undo);
        motion::Id id = 0;
        beginTest("Live source settings save and reopen with fresh runtime identity and no embedded geometry");
        const auto port = freePort();
        expect(port != 0);
        if (port == 0) { return; }
        expect(document.addBlenderSource("Live drawing", {port, true}, id).wasOk());
        auto asset = document.mainProject().assets.front();
        const auto xml = document.save();
        expect(xml.getChildByName("asset")->getChildByName("blender") != nullptr);
        motion::Project reopened;
        expect(motion::Document::prepareLoad(xml, reopened).wasOk());
        expect(reopened.assets.size() == 1);
        expect(reopened.assets.front()->liveIdentity != asset->liveIdentity);
        expect(reopened.assets.front()->blenderSettings.port == port && reopened.assets.front()->blenderSettings.freezeOnDisconnect);
        expect(reopened.assets.front()->source == nullptr && reopened.assets.front()->data.getSize() == 0);
        auto invalid = xml;
        invalid.getChildByName("asset")->getChildByName("blender")->setAttribute("disconnect", "unknown");
        motion::Project rejected;
        expect(motion::Document::prepareLoad(invalid, rejected).failed());
        invalid = xml;
        invalid.getChildByName("asset")->getChildByName("blender")->setAttribute("port", 1);
        expect(motion::Document::prepareLoad(invalid, rejected).failed());
        invalid = xml;
        invalid.getChildByName("asset")->createNewChildElement("blender");
        expect(motion::Document::prepareLoad(invalid, rejected).failed());

        std::shared_ptr<const motion::LiveSourceFrames> frames;
        motion::LiveBlenderController controller(document, [&](auto value) { frames = std::move(value); });
        beginTest("Explicit listening prepares actual socket geometry without changing document revision");
        expect(!controller.listening(id));
        expect(controller.listen(id, true).wasOk());
        expect(wait([&] { return controller.listening(id); }));
        const auto revision = document.revision();
        juce::StreamingSocket sender;
        expect(sender.connect("127.0.0.1", port, 1000));
        expect(send(sender, packet(.25)));
        expect(wait([&] { controller.poll(); return frames != nullptr && frames->resolve(asset->liveIdentity.get()) != nullptr; }));
        if (frames == nullptr || frames->resolve(asset->liveIdentity.get()) == nullptr) { return; }
        expectWithinAbsoluteError(frames->resolve(asset->liveIdentity.get())->sample(0, .5).x, .25f, 1e-6f);
        expect(document.revision() == revision);
        beginTest("Disconnect freezes last geometry and changing policy blanks without restarting the source identity");
        expect(send(sender, "CLOSE\n")); sender.close();
        expect(wait([&] { return controller.statusText(id).contains("Waiting"); }));
        controller.poll();
        expect(frames->resolve(asset->liveIdentity.get()) != nullptr);
        expect(document.setBlenderSource(id, "Live drawing", {port, false}).wasOk());
        expect(document.mainProject().assets.front()->liveIdentity == asset->liveIdentity);
        controller.poll();
        expect(frames->resolve(asset->liveIdentity.get()) == nullptr);
        expect(undo.undo()); controller.poll();
        expect(frames->resolve(asset->liveIdentity.get()) != nullptr);

        beginTest("Reconnection accepts a new frame and an intentional empty frame replaces the frozen drawing");
        expect(sender.connect("127.0.0.1", port, 1000));
        expect(send(sender, packet(.5)));
        expect(wait([&] { controller.poll(); const auto* source = frames->resolve(asset->liveIdentity.get()); return source != nullptr && source->sample(0, .5).x == .5f; }));
        expect(send(sender, packet(0, true)));
        expect(wait([&] { controller.poll(); const auto* source = frames->resolve(asset->liveIdentity.get()); return source != nullptr && source->sample(0, .5).r == 0; }));

        beginTest("Make source unique assigns a different live identity and cannot alias a listener");
        document.edit("Two instances", [&](motion::Project& project) {
            for (int index = 0; index < 2; ++index) {
                motion::Track track; track.id = document.newId();
                track.clips.push_back(motion::Document::makeClip(document.newId(), *asset, 0));
                project.tracks.push_back(track);
            }
        });
        auto copy = std::make_shared<motion::Asset>(*asset);
        const auto current = document.mainProject().assets.front();
        expect(document.makeSourceUnique(document.project().tracks.front().clips.front().id, current, copy).wasOk());
        expect(copy->liveIdentity != asset->liveIdentity);
        controller.poll();
        expect(frames->resolve(copy->liveIdentity.get()) == nullptr);
        expect(controller.listen(copy->id, true).failed());

        beginTest("Project replacement prunes the listener and old snapshots cannot resolve the reopened source");
        document.reset(std::move(reopened)); controller.poll();
        const auto fresh = document.mainProject().assets.front();
        expect(!controller.listening(fresh->id));
        expect(frames->resolve(fresh->liveIdentity.get()) == nullptr);
        expect(controller.statusText(fresh->id).contains("Offline"));
        juce::StreamingSocket probe;
        expect(probe.createListener(port, "127.0.0.1"), "Project replacement released the old listener");
        sender.close();
    }
private:
    template<class Predicate> static bool wait(Predicate predicate) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000;
        while (!predicate()) { if (juce::Time::getMillisecondCounterHiRes() >= deadline) { return false; } juce::Thread::sleep(5); }
        return true;
    }
    static int freePort() {
        for (int port = 51600; port <= 51699; ++port) { juce::StreamingSocket probe; if (probe.createListener(port, "127.0.0.1")) { return port; } }
        return 0;
    }
    static bool send(juce::StreamingSocket& socket, const juce::String& value) { return socket.write(value.toRawUTF8(), static_cast<int>(value.getNumBytesAsUTF8())) == static_cast<int>(value.getNumBytesAsUTF8()); }
    static juce::String packet(double x, bool empty = false) {
        juce::MemoryOutputStream bytes;
        const auto tag = [&](const char* text) { bytes.write(text, 8); };
        const auto integer = [&](std::uint64_t value) { bytes.writeInt64(static_cast<juce::int64>(value)); };
        const auto number = [&](double value) { integer(std::bit_cast<std::uint64_t>(value)); };
        tag("GPLA    "); integer(2); integer(0); integer(0);
        tag("FILE    "); tag("fCount  "); integer(1); tag("fRate   "); integer(24); tag("DONE    ");
        tag("FRAME   "); tag("focalLen"); number(-1); tag("OBJECTS ");
        if (!empty) {
            tag("OBJECT  "); tag("MATRIX  ");
            for (int index = 0; index < 16; ++index) { number(index % 5 == 0 ? 1 : 0); }
            tag("DONE    "); tag("STROKES "); tag("STROKE  "); tag("vertexCt"); integer(2); tag("VERTICES");
            number(x); number(0); number(-1); number(x); number(1); number(-1);
            for (int index = 0; index < 4; ++index) { tag("DONE    "); }
        }
        tag("DONE    "); tag("DONE    "); tag("END GPLA");
        return juce::Base64::toBase64(bytes.getData(), bytes.getDataSize()) + "\n";
    }
};
static MotionBlenderLifecycleTest motionBlenderLifecycleTest;
