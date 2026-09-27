#pragma once

#include "BlenderReceiver.h"
#include "PreparedBlenderFrame.h"

namespace motion {
// Owner-thread lifecycle, receiver-thread parsing, preparation-thread geometry.
// No socket or frame preparation is performed by the audio callback.
class PreparedBlenderInput final : private juce::Thread {
public:
    struct Frame {
        std::shared_ptr<const PreparedSource> source;
        std::uint64_t connection = 0, serial = 0;
    };
    PreparedBlenderInput() : juce::Thread("Prepare Blender geometry") { if (!startThread()) { failed.store(true); } }
    ~PreparedBlenderInput() override {
        signalThreadShouldExit();
        notify();
        waitForThreadToExit(-1);
        receiver.stop();
    }
    void listen(int port) { receiver.start(port); }
    void stop() { receiver.stop(); }
    BlenderReceiver::Status status() const { return receiver.getStatus(); }
    Frame frame() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return prepared;
    }
private:
    void run() override {
        while (!threadShouldExit()) {
            const auto incoming = receiver.snapshot();
            if (incoming.frame != nullptr && (incoming.connection != connection || incoming.serial != serial)) {
                try {
                    auto source = prepareBlenderFrame(*incoming.frame);
                    Frame old;
                    {
                        const juce::SpinLock::ScopedLockType guard(lock);
                        old = std::move(prepared);
                        prepared = {std::move(source), incoming.connection, incoming.serial};
                    }
                    failed.store(false);
                    connection = incoming.connection;
                    serial = incoming.serial;
                } catch (...) {
                    // Retain the last prepared frame; the owner can report a
                    // preparation failure without publishing partial geometry.
                    failed.store(true);
                }
            }
            wait(16);
        }
    }
    BlenderReceiver receiver;
    mutable juce::SpinLock lock;
    Frame prepared;
    std::uint64_t connection = 0, serial = 0;
    std::atomic<bool> failed{false};
public:
    bool preparationFailed() const { return failed.load(); }
};
}
