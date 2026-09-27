#include <JuceHeader.h>
#include "../Source/motion/live/LiveBlenderController.h"
#include "../Source/motion/live/BlenderCaptureArchive.h"
#include "../Source/motion/export/SignalExporter.h"
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

        beginTest("Receiver capture retains actual frame transitions and source-local timing");
        const auto revisionBeforeCapture = document.revision();
        expect(controller.beginCapture(id).wasOk());
        expect(controller.capturing(id));
        juce::Thread::sleep(8);
        expect(send(sender, packet(.75)));
        expect(wait([&] {
            controller.poll();
            const auto* source = frames != nullptr ? frames->resolve(asset->liveIdentity.get()) : nullptr;
            return source != nullptr && source->sample(0, .5).x == .75f;
        }));
        juce::Thread::sleep(8);
        expect(send(sender, packet(-.25)));
        expect(wait([&] {
            controller.poll();
            const auto* source = frames != nullptr ? frames->resolve(asset->liveIdentity.get()) : nullptr;
            return source != nullptr && source->sample(0, .5).x == -.25f;
        }));
        juce::Thread::sleep(8);
        auto recorded = controller.finishCapture(id);
        expect(recorded != nullptr && recorded->failure == motion::BlenderCapture::Failure::none);
        if (recorded == nullptr || recorded->failure != motion::BlenderCapture::Failure::none) { return; }
        expect(!controller.capturing(id) && document.revision() == revisionBeforeCapture);
        const bool recordedTransitions = recorded->frames.size() >= 3;
        expect(recordedTransitions && recorded->frames.front().geometry != nullptr && recorded->frames.front().geometry->segments.empty(),
            "Capture starts from the current intentional blank frame and records later receiver frames");
        if (!recordedTransitions) { return; }
        expect(recorded->frames[1].start > 0 && recorded->frames[2].start > recorded->frames[1].start && recorded->duration > recorded->frames.back().start,
            "Receiver timestamps remain increasing and final hold extends the last frame");
        const auto archive = motion::BlenderCaptureArchive::encode(*recorded);
        expect(static_cast<bool>(archive), archive.error);
        const auto decodedCapture = motion::BlenderCaptureArchive::decode(archive.bytes);
        expect(static_cast<bool>(decodedCapture) && decodedCapture.frames.size() == recorded->frames.size(), decodedCapture.error);
        if (!decodedCapture || decodedCapture.frames.size() != recorded->frames.size()) { return; }
        expect(decodedCapture.frames.front()->segments.empty() && decodedCapture.frames[1]->segments.size() == 1
            && decodedCapture.frames[2]->segments.size() == 1, "Capture archive preserves blank and line-frame transitions");
        if (decodedCapture.frames[1]->segments.size() == 1 && decodedCapture.frames[2]->segments.size() == 1) {
            expectWithinAbsoluteError(decodedCapture.frames[1]->segments.front().x1, .75, 1.0e-12,
                "Capture retains the first receiver geometry");
            expectWithinAbsoluteError(decodedCapture.frames[2]->segments.front().x1, -.25, 1.0e-12,
                "Capture retains the second receiver geometry");
        }
        expect(decodedCapture.timing->duration() > decodedCapture.timing->frameEnd(decodedCapture.timing->frameCount() - 2),
            "Decoded capture retains a nonzero final-frame hold");

        beginTest("Cancelled receiver capture leaves no archive or document mutation");
        expect(controller.beginCapture(id).wasOk());
        expect(controller.capturing(id));
        controller.cancelCapture(id);
        expect(!controller.capturing(id) && controller.finishCapture(id) == nullptr && document.revision() == revisionBeforeCapture);

        beginTest("Disconnected blank policy seeds a blank capture despite cached receiver geometry");
        expect(send(sender, "CLOSE\n"));
        sender.close();
        expect(wait([&] { return controller.statusText(id).contains("Waiting"); }));
        expect(document.setBlenderSource(id, "Live drawing", {port, false}).wasOk());
        controller.poll();
        expect(frames->resolve(asset->liveIdentity.get()) == nullptr, "The live preview blanks while disconnected");
        expect(controller.beginCapture(id).wasOk());
        juce::Thread::sleep(8);
        auto disconnectedCapture = controller.finishCapture(id);
        expect(disconnectedCapture != nullptr && disconnectedCapture->failure == motion::BlenderCapture::Failure::none);
        if (disconnectedCapture == nullptr || disconnectedCapture->failure != motion::BlenderCapture::Failure::none) { return; }
        expect(disconnectedCapture->frames.size() == 1 && disconnectedCapture->frames.front().geometry == nullptr
            && disconnectedCapture->duration > 0, "Capture begins blank instead of reusing a cached disconnected frame");

        beginTest("Captured vector assets decode, reopen and export without a live identity");
        auto captured = std::make_shared<motion::Asset>();
        captured->id = 500; captured->name = "Recorded Blender"; captured->extension = ".blender-capture";
        captured->data.append(archive.bytes.data(), archive.bytes.size());
        const auto decoded = motion::Document::decodeAsset(*captured);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        expect(captured->liveIdentity == nullptr && captured->source != nullptr && captured->source->frameCount() == decodedCapture.frames.size());
        if (decoded.failed() || captured->source == nullptr) { return; }
        motion::Project capturedProject;
        capturedProject.duration = std::max(1.0, captured->source->duration());
        capturedProject.assets.push_back(captured);
        motion::Track capturedTrack; capturedTrack.id = 501;
        auto capturedClip = motion::Document::makeClip(502, *captured, 0);
        capturedTrack.clips.push_back(capturedClip);
        capturedProject.tracks.push_back(std::move(capturedTrack));
        juce::UndoManager capturedUndo;
        motion::Document capturedDocument(capturedUndo);
        capturedDocument.reset(capturedProject);
        motion::Project restoredCapture;
        expect(motion::Document::prepareLoad(capturedDocument.save(), restoredCapture).wasOk());
        expect(restoredCapture.assets.size() == 1 && restoredCapture.assets.front()->liveIdentity == nullptr
            && restoredCapture.assets.front()->source != nullptr && restoredCapture.assets.front()->source->duration() == captured->source->duration(),
            "Saved capture reopens as an immutable source with its duration");
        juce::TemporaryFile exportFile(".wav");
        const std::atomic<bool> notCancelled {false};
        const auto exported = motion::SignalExporter::write(capturedProject, exportFile.getFile(), 48000, notCancelled);
        expect(exported.wasOk(), exported.getErrorMessage());

        beginTest("Single-frame capture persists its measured final hold");
        auto singleFrame = std::make_shared<motion::BlenderFrame>();
        singleFrame->frameRate = 24;
        singleFrame->segments.push_back({-.2, -.1, .3, .4});
        motion::BlenderCapture single(true);
        single.begin(0, singleFrame); single.finish(.0125);
        const auto singleArchive = motion::BlenderCaptureArchive::encode(single);
        expect(static_cast<bool>(singleArchive), singleArchive.error);
        auto singleAsset = std::make_shared<motion::Asset>();
        singleAsset->id = 600; singleAsset->name = "One frame"; singleAsset->extension = ".blender-capture";
        singleAsset->data.append(singleArchive.bytes.data(), singleArchive.bytes.size());
        expect(motion::Document::decodeAsset(*singleAsset).wasOk());
        expect(singleAsset->liveIdentity == nullptr && singleAsset->source != nullptr && singleAsset->source->frameCount() == 1
            && std::abs(singleAsset->source->duration() - .0125) < 1.0e-12, "Single-frame capture keeps measured duration instead of Blender frame rate");
        if (singleAsset->source != nullptr) {
            const auto singleClip = motion::Document::makeClip(601, *singleAsset, 0);
            expectWithinAbsoluteError(singleClip.duration, .0125, 1.0e-12,
                "Single-frame captured assets create clips with their measured duration");
            motion::Project singleProject;
            singleProject.duration = 1;
            singleProject.assets.push_back(singleAsset);
            juce::UndoManager singleUndo;
            motion::Document singleDocument(singleUndo);
            singleDocument.reset(singleProject);
            motion::Project restoredSingle;
            expect(motion::Document::prepareLoad(singleDocument.save(), restoredSingle).wasOk());
            expect(restoredSingle.assets.size() == 1 && restoredSingle.assets.front()->liveIdentity == nullptr
                && restoredSingle.assets.front()->source != nullptr
                && std::abs(restoredSingle.assets.front()->source->duration() - .0125) < 1.0e-12,
                "Reopened single-frame capture retains its measured final hold");
        }

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
