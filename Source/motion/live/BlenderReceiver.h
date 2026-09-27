#pragma once

#include <juce_core/juce_core.h>
#include "BlenderFrame.h"
#include "BlenderCapture.h"
#include <memory>
#include <string>

namespace motion {
// A loopback-only receiver. Its socket, framing, decode and frame destruction
// belong to the worker. Consumers take immutable snapshots off the audio thread;
// this is deliberately not an audio-thread publication primitive.
class BlenderReceiver final : private juce::Thread {
public:
    enum class State { stopped, listening, connected, failed };
    struct Status {
        State state = State::stopped;
        int port = 0;
        std::uint64_t connection = 0, acceptedFrames = 0, rejectedFrames = 0;
        juce::String message;
    };
    struct Snapshot {
        std::shared_ptr<const BlenderFrame> frame;
        std::uint64_t connection = 0, serial = 0;
        double receivedSeconds = 0;
    };

    BlenderReceiver() : juce::Thread("Motion Blender input") {}
    ~BlenderReceiver() override { stop(); }

    // Owner thread only. Port zero is useful for isolated tests; a Blender UI
    // endpoint should choose a port supported by the installed add-on.
    void start(int port) {
        stop();
        cancelCapture();
        Snapshot old;
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            status = {};
            status.port = port;
            old = std::move(latest);
            latest = {};
            if (port < 0 || port > 65535) {
                status.state = State::failed;
                status.message = "Choose a valid local port.";
                return;
            }
        }
        requestedPort = port;
        if (!startThread()) { setState(State::failed, "Could not start the Blender receiver."); }
    }
    void stop() {
        signalThreadShouldExit();
        // Network waits are bounded; no caller closes a socket while the worker
        // is reading it. Last good geometry remains available for freeze/capture.
        waitForThreadToExit(-1);
        setState(State::stopped, {});
    }
    Status getStatus() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return status;
    }
    Snapshot snapshot() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return latest;
    }

    bool beginCapture(bool freeze) {
        auto next = std::make_unique<BlenderCapture>(freeze);
        const juce::SpinLock::ScopedLockType guard(lock);
        if (capture != nullptr) { return false; }
        const bool visible = freeze || (status.state == State::connected && latest.connection == status.connection);
        next->begin(nowSeconds(), visible ? latest.frame : nullptr);
        capture = std::move(next);
        return true;
    }
    std::unique_ptr<BlenderCapture> finishCapture() {
        const juce::SpinLock::ScopedLockType guard(lock);
        if (capture != nullptr) { capture->finish(nowSeconds()); }
        return std::move(capture);
    }
    void cancelCapture() {
        std::unique_ptr<BlenderCapture> old;
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            old = std::move(capture);
        }
    }
    juce::String captureError() {
        const juce::SpinLock::ScopedLockType guard(lock);
        if (capture == nullptr) { return {}; }
        capture->checkTime(nowSeconds());
        return capture->error();
    }
    bool capturing() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return capture != nullptr;
    }

    static constexpr std::size_t maximumMessageBytes = 10 * 1024 * 1024;

private:
    static double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    void setState(State state, juce::String message) {
        const juce::SpinLock::ScopedLockType guard(lock);
        if (capture != nullptr && !capture->freezeOnDisconnect && state != State::connected) { capture->append(nowSeconds(), nullptr); }
        status.state = state;
        status.message = std::move(message);
    }
    void reject(juce::String message) {
        const juce::SpinLock::ScopedLockType guard(lock);
        ++status.rejectedFrames;
        status.message = std::move(message);
    }
    static bool validBase64(std::string_view text) {
        if (text.empty() || text.size() % 4 != 0) { return false; }
        std::size_t padding = 0;
        if (text.back() == '=') { ++padding; }
        if (text.size() > 1 && text[text.size() - 2] == '=') { ++padding; }
        for (std::size_t index = 0; index < text.size() - padding; ++index) {
            const auto ch = text[index];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                || (ch >= '0' && ch <= '9') || ch == '+' || ch == '/')) { return false; }
        }
        return true;
    }
    bool consume(std::string_view message) {
        if (!message.empty() && message.back() == '\r') { message.remove_suffix(1); }
        if (message == "CLOSE") { return false; }
        if (message.empty()) { return true; }
        if (!validBase64(message) || !message.starts_with("R1BMQSAg")) {
            reject("Invalid Blender frame encoding.");
            return true;
        }
        juce::MemoryOutputStream binary;
        if (!juce::Base64::convertFromBase64(binary, juce::String::fromUTF8(message.data(), static_cast<int>(message.size())))) {
            reject("Invalid Blender frame encoding.");
            return true;
        }
        auto decoded = decodeBlenderFrame({static_cast<const std::uint8_t*>(binary.getData()), binary.getDataSize()});
        if (!decoded.frame.has_value()) {
            reject(juce::String(decoded.error));
            return true;
        }
        auto frame = std::make_shared<const BlenderFrame>(std::move(*decoded.frame));
        Snapshot old;
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            old = std::move(latest);
            latest = {std::move(frame), status.connection, ++status.acceptedFrames, nowSeconds()};
            if (capture != nullptr) { capture->append(latest.receivedSeconds, latest.frame); }
            status.message.clear();
        }
        return true;
    }
    void run() override {
        try { receive(); }
        catch (...) { setState(State::failed, "Could not prepare the incoming Blender frame."); }
    }
    void receive() {
        juce::StreamingSocket listener;
        if (!listener.createListener(requestedPort, "127.0.0.1")) {
            setState(State::failed, "The local Blender port is already in use or unavailable.");
            return;
        }
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            status.port = listener.getBoundPort();
            status.state = State::listening;
        }
        std::string message;
        message.reserve(65536);
        while (!threadShouldExit()) {
            const auto ready = listener.waitUntilReady(true, 100);
            if (ready < 0) { setState(State::failed, "The Blender listener stopped unexpectedly."); return; }
            if (ready == 0) { continue; }
            std::unique_ptr<juce::StreamingSocket> connection(listener.waitForNextConnection());
            if (connection == nullptr || threadShouldExit()) { continue; }
            {
                const juce::SpinLock::ScopedLockType guard(lock);
                status.state = State::connected;
                status.connection = ++connectionSerial;
                status.message.clear();
            }
            message.clear();
            bool open = true;
            while (open && !threadShouldExit()) {
                const auto readable = connection->waitUntilReady(true, 100);
                if (readable < 0) { break; }
                if (readable == 0) { continue; }
                char bytes[4096];
                const auto count = connection->read(bytes, sizeof(bytes), false);
                if (count <= 0) { break; }
                for (int index = 0; index < count && open && !threadShouldExit(); ++index) {
                    const auto ch = bytes[index];
                    if (ch == '\n') {
                        open = consume(message);
                        message.clear();
                    } else if (ch == '\0' || message.size() >= maximumMessageBytes) {
                        reject("Blender message exceeds the input limit or contains a null byte.");
                        open = false;
                    } else {
                        message.push_back(ch);
                    }
                }
            }
            connection->close();
            if (!threadShouldExit()) {
                setState(State::listening, {});
            }
        }
    }
    mutable juce::SpinLock lock;
    Status status;
    Snapshot latest;
    std::unique_ptr<BlenderCapture> capture;
    int requestedPort = 0;
    std::uint64_t connectionSerial = 0; // Never reused across stop/start; worker owns it while running.
};
}
