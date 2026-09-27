#include <JuceHeader.h>
#include "../Source/motion/live/BlenderReceiver.h"

class MotionBlenderReceiverTest : public juce::UnitTest {
public:
    MotionBlenderReceiverTest() : juce::UnitTest("Motion bounded live Blender receiver", "MotionLive") {}
    void runTest() override {
        using State = motion::BlenderReceiver::State;
        motion::BlenderReceiver receiver;
        beginTest("Loopback listener accepts fragmented addon GPLA snapshots and publishes immutable latest frames");
        receiver.start(0);
        expect(wait([&] { return receiver.getStatus().state == State::listening; }));
        const auto port = receiver.getStatus().port;
        expect(port > 0);
        juce::StreamingSocket client;
        if (!expectConnect(client, port)) { return; }
        const auto first = encodedFrame(1);
        expect(send(client, first.substring(0, 13)));
        expect(send(client, first.substring(13)));
        expect(wait([&] { return receiver.snapshot().serial == 1; }));
        const auto held = receiver.snapshot();
        expect(held.frame != nullptr);
        if (held.frame == nullptr) { return; }
        expectEquals(static_cast<int>(held.frame->segments.size()), 1);
        expectWithinAbsoluteError(held.frame->segments.front().x1, 1.0, 1e-12);
        expect(send(client, encodedFrame(2) + encodedFrame(3) + encodedFrame(4)));
        expect(wait([&] { return receiver.snapshot().serial == 4; }));
        expectWithinAbsoluteError(receiver.snapshot().frame->segments.front().x1, 4.0, 1e-12);
        expectWithinAbsoluteError(held.frame->segments.front().x1, 1.0, 1e-12);
        expect(receiver.snapshot().receivedSeconds >= held.receivedSeconds);

        beginTest("Malformed payloads reject without publishing fallback geometry and a following valid frame recovers");
        const auto rejects = receiver.getStatus().rejectedFrames;
        expect(send(client, "%%%\nR1BMQSAgAAAA\n"));
        expect(wait([&] { return receiver.getStatus().rejectedFrames == rejects + 2; }));
        expectEquals(receiver.snapshot().serial, std::uint64_t(4));
        expect(receiver.getStatus().message.isNotEmpty());
        expect(send(client, encodedFrame(5)));
        expect(wait([&] { return receiver.snapshot().serial == 5; }));
        expect(receiver.getStatus().message.isEmpty());

        beginTest("Close ignores trailing data and reconnects with a new connection identity");
        const auto generation = receiver.getStatus().connection;
        expect(send(client, "CLOSE\n" + encodedFrame(99)));
        expect(wait([&] { return receiver.getStatus().state == State::listening; }));
        expectEquals(receiver.snapshot().serial, std::uint64_t(5));
        client.close();
        expectConnect(client, port);
        expect(wait([&] { return receiver.getStatus().connection > generation; }));
        expect(send(client, encodedFrame(6)));
        expect(wait([&] { return receiver.snapshot().serial == 6; }));
        expectEquals(receiver.snapshot().connection, generation + 1);

        beginTest("A null byte terminates a malformed stream while retaining the last valid frame");
        const char invalid[] {'x', '\0', '\n'};
        expectEquals(client.write(invalid, sizeof(invalid)), static_cast<int>(sizeof(invalid)));
        expect(wait([&] { return receiver.getStatus().state == State::listening; }));
        expectEquals(receiver.snapshot().serial, std::uint64_t(6));
        client.close();

        beginTest("Bind failure is reported without replacing another receiver");
        motion::BlenderReceiver occupied;
        occupied.start(port);
        expect(wait([&] { return occupied.getStatus().state == State::failed; }));
        expect(occupied.getStatus().message.isNotEmpty());
        expectEquals(static_cast<int>(receiver.getStatus().state), static_cast<int>(State::listening));

        beginTest("Idle connected shutdown is bounded and explicit restart clears the old snapshot");
        expectConnect(client, port);
        expect(wait([&] { return receiver.getStatus().state == State::connected; }));
        const auto before = juce::Time::getMillisecondCounterHiRes();
        receiver.stop();
        expect(juce::Time::getMillisecondCounterHiRes() - before < 1000);
        expectEquals(static_cast<int>(receiver.getStatus().state), static_cast<int>(State::stopped));
        expectEquals(receiver.snapshot().serial, std::uint64_t(6));
        client.close();
        receiver.start(0);
        expect(wait([&] { return receiver.getStatus().state == State::listening; }));
        expect(receiver.snapshot().frame == nullptr);
        expectConnect(client, receiver.getStatus().port);
        expect(send(client, encodedFrame(7)));
        expect(wait([&] { return receiver.snapshot().serial == 1; }));
        expect(receiver.snapshot().connection > generation + 1, "Restart must not reuse stale connection identities");
        beginTest("Intentional blank snapshots replace geometry, while oversized messages terminate the stream");
        expect(send(client, encodedBlankFrame()));
        expect(wait([&] { return receiver.snapshot().serial == 2; }));
        expect(receiver.snapshot().frame != nullptr && receiver.snapshot().frame->segments.empty());
        const std::string overlong(motion::BlenderReceiver::maximumMessageBytes + 1, 'A');
        std::size_t sent = 0;
        while (sent < overlong.size()) {
            const auto count = client.write(overlong.data() + sent, static_cast<int>(std::min<std::size_t>(4096, overlong.size() - sent)));
            if (count <= 0) { break; }
            sent += static_cast<std::size_t>(count);
        }
        expect(wait([&] { return receiver.getStatus().state == State::listening; }));
        expectEquals(receiver.getStatus().rejectedFrames, std::uint64_t(1));
        expectEquals(receiver.snapshot().serial, std::uint64_t(2));
        client.close();
        receiver.stop();
        testActualBlender(receiver);
        receiver.start(-1);
        expectEquals(static_cast<int>(receiver.getStatus().state), static_cast<int>(State::failed));
    }

private:
    void testActualBlender(motion::BlenderReceiver& receiver) {
        const auto executable = juce::SystemStats::getEnvironmentVariable("MOTION_BLENDER_EXECUTABLE", {});
        const auto root = juce::SystemStats::getEnvironmentVariable("MOTION_BLENDER_REPOSITORY", {});
        if (executable.isEmpty() || root.isEmpty()) {
            logMessage("Actual Blender process integration not run: set MOTION_BLENDER_EXECUTABLE and MOTION_BLENDER_REPOSITORY.");
            return;
        }
        beginTest("Actual Blender addon connect, evaluated Grease Pencil update and CLOSE reach the bounded receiver");
        using State = motion::BlenderReceiver::State;
        for (int port = 51600; port < 51700; ++port) {
            receiver.start(port);
            expect(wait([&] { return receiver.getStatus().state == State::listening || receiver.getStatus().state == State::failed; }));
            if (receiver.getStatus().state == State::listening) { break; }
        }
        if (receiver.getStatus().state != State::listening) { expect(false, "No Blender addon port available"); return; }
        juce::ChildProcess child;
        const auto directory = juce::File(root);
        const juce::StringArray arguments {executable, "--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "1",
            "--python", directory.getChildFile("scripts/send_motion_blender_fixture.py").getFullPathName(), "--",
            directory.getChildFile("blender/osci_render/__init__.py").getFullPathName(), juce::String(receiver.getStatus().port)};
        if (!child.start(arguments)) { expect(false, "Could not launch Blender fixture"); return; }
        const auto finished = child.waitForProcessToFinish(30000);
        if (!finished) { child.kill(); }
        const auto output = child.readAllProcessOutput();
        logMessage(output);
        expect(finished, "Blender fixture timed out");
        expectEquals(static_cast<int>(child.getExitCode()), 0);
        expect(wait([&] { return receiver.getStatus().state == State::listening; }));
        const auto snapshot = receiver.snapshot();
        expect(snapshot.serial >= 2);
        expectEquals(receiver.getStatus().rejectedFrames, std::uint64_t(0));
        expect(snapshot.frame != nullptr);
        if (snapshot.frame != nullptr) {
            expectEquals(static_cast<int>(snapshot.frame->segments.size()), 3);
            if (!snapshot.frame->segments.empty()) {
                expectWithinAbsoluteError(snapshot.frame->segments.front().x1, 1.0, 1e-6);
                expectWithinAbsoluteError(snapshot.frame->segments.front().y1, 0.0, 1e-6);
            }
        }
        receiver.stop();
    }

    template <typename Predicate> static bool wait(Predicate predicate) {
        const auto until = juce::Time::getMillisecondCounterHiRes() + 3000;
        while (!predicate()) {
            if (juce::Time::getMillisecondCounterHiRes() > until) { return false; }
            juce::Thread::sleep(5);
        }
        return true;
    }
    bool expectConnect(juce::StreamingSocket& client, int port) {
        const auto connected = client.connect("127.0.0.1", port, 1000);
        expect(connected);
        return connected;
    }
    static bool send(juce::StreamingSocket& socket, const juce::String& text) {
        const auto* bytes = text.toRawUTF8();
        int remaining = static_cast<int>(text.getNumBytesAsUTF8());
        while (remaining > 0) {
            const auto sent = socket.write(bytes, remaining);
            if (sent <= 0) { return false; }
            bytes += sent; remaining -= sent;
        }
        return true;
    }
    static juce::String encodedBlankFrame() {
        juce::MemoryOutputStream binary;
        const auto tag = [&](const char* value) { binary.write(value, 8); };
        tag("GPLA    "); binary.writeInt64(2); binary.writeInt64(0); binary.writeInt64(0);
        tag("FILE    "); tag("fCount  "); binary.writeInt64(1); tag("fRate   "); binary.writeInt64(30); tag("DONE    ");
        tag("FRAME   "); tag("focalLen"); binary.writeDouble(-1); tag("OBJECTS "); tag("DONE    "); tag("DONE    "); tag("END GPLA");
        return juce::Base64::toBase64(binary.getData(), binary.getDataSize()) + "\n";
    }
    static juce::String encodedFrame(double x) {
        juce::MemoryOutputStream binary;
        const auto tag = [&](const char* value) { binary.write(value, 8); };
        tag("GPLA    "); binary.writeInt64(2); binary.writeInt64(0); binary.writeInt64(0);
        tag("FILE    "); tag("fCount  "); binary.writeInt64(1); tag("fRate   "); binary.writeInt64(30); tag("DONE    ");
        tag("FRAME   "); tag("focalLen"); binary.writeDouble(-1); tag("OBJECTS ");
        tag("OBJECT  "); tag("MATRIX  ");
        for (int index = 0; index < 16; ++index) { binary.writeDouble(index % 5 == 0 ? 1 : 0); }
        tag("DONE    "); tag("STROKES "); tag("STROKE  "); tag("vertexCt"); binary.writeInt64(2); tag("VERTICES");
        binary.writeDouble(x); binary.writeDouble(0); binary.writeDouble(-1);
        binary.writeDouble(x + .25); binary.writeDouble(.5); binary.writeDouble(-1);
        for (int index = 0; index < 6; ++index) { tag("DONE    "); }
        tag("END GPLA");
        return juce::Base64::toBase64(binary.getData(), binary.getDataSize()) + "\n";
    }
};
static MotionBlenderReceiverTest motionBlenderReceiverTest;
