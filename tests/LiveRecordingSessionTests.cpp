#include <JuceHeader.h>

#include "../Source/recording/LiveRecordingSession.h"

#include <thread>

namespace {

struct VideoBackendState {
    juce::WaitableEvent writeEntered;
    juce::WaitableEvent allowWrite;
    std::atomic<int> writes { 0 };
    std::atomic<bool> blockWrites { false };
    bool running = false;
};

class FakeVideoRecordingBackend final : public VideoRecordingBackend {
public:
    explicit FakeVideoRecordingBackend(std::shared_ptr<VideoBackendState> state) : state(std::move(state)) {}

    bool start(const juce::String& command) override {
        state->running = command.isNotEmpty();
        return state->running;
    }

    bool write(std::span<const std::uint8_t> frame) override {
        if (frame.empty()) {
            return false;
        }
        state->writeEntered.signal();
        if (state->blockWrites.exchange(false)) {
            state->allowWrite.wait(5000);
        }
        ++state->writes;
        return true;
    }

    void finish() override { state->running = false; }

private:
    std::shared_ptr<VideoBackendState> state;
};

struct AudioBackendState {
    int writes = 0;
    bool running = false;
};

class FakeAudioRecordingBackend final : public AudioRecordingBackend {
public:
    explicit FakeAudioRecordingBackend(std::shared_ptr<AudioBackendState> state) : state(std::move(state)) {}

    bool start(const juce::File&, double sampleRate) override {
        state->running = sampleRate > 0.0;
        return state->running;
    }
    void write(const juce::AudioBuffer<float>&) override { ++state->writes; }
    void finish() override { state->running = false; }

private:
    std::shared_ptr<AudioBackendState> state;
};

class LiveRecordingSessionTest final : public juce::UnitTest {
public:
    LiveRecordingSessionTest() : juce::UnitTest("Live recording session", "Recording") {}

    void runTest() override {
        beginTest("Capture backends are driven independently of the visualiser");
        auto videoState = std::make_shared<VideoBackendState>();
        auto audioState = std::make_shared<AudioBackendState>();
        LiveRecordingSession session(std::make_unique<FakeVideoRecordingBackend>(videoState),
                                     std::make_unique<FakeAudioRecordingBackend>(audioState));
        LiveRecordingConfiguration configuration {
            .captureVideo = true,
            .captureAudio = true,
            .videoWidth = 2,
            .videoHeight = 3,
            .sampleRate = 48000.0,
            .videoFileExtension = "mov",
        };
        configuration.buildVideoCommand = [](const juce::File&) { return juce::String("fake encoder"); };
        expect(static_cast<bool>(session.start(configuration)));
        auto frame = session.acquireVideoFrame();
        expectEquals(static_cast<int>(frame.getBytes().size()), 24);
        std::fill(frame.getBytes().begin(), frame.getBytes().end(), std::uint8_t { 7 });
        frame.submit();
        juce::AudioBuffer<float> audio(2, 16);
        session.writeAudioBlock(audio);
        auto artifacts = session.finish();
        expectEquals(videoState->writes.load(), 1);
        expectEquals(audioState->writes, 1);
        expect(artifacts.hasVideo());
        expect(artifacts.hasAudio());
        expectEquals(artifacts.getOutputFileExtension(), juce::String("mov"));

        beginTest("Finishing waits for an acquired frame to be released");
        videoState = std::make_shared<VideoBackendState>();
        audioState = std::make_shared<AudioBackendState>();
        LiveRecordingSession guardedSession(std::make_unique<FakeVideoRecordingBackend>(videoState),
                                            std::make_unique<FakeAudioRecordingBackend>(audioState));
        configuration.captureAudio = false;
        expect(static_cast<bool>(guardedSession.start(configuration)));
        auto heldFrame = guardedSession.acquireVideoFrame();
        expect(heldFrame.isValid());
        std::atomic<bool> finishReturned { false };
        std::thread finishThread([&] {
            guardedSession.finish();
            finishReturned.store(true);
        });
        juce::Thread::sleep(20);
        expect(!finishReturned.load());
        heldFrame = {};
        finishThread.join();
        expect(finishReturned.load());

        beginTest("A pending video frame applies backpressure without dropping frames");
        videoState = std::make_shared<VideoBackendState>();
        videoState->blockWrites = true;
        audioState = std::make_shared<AudioBackendState>();
        LiveRecordingSession slowSession(std::make_unique<FakeVideoRecordingBackend>(videoState),
                                         std::make_unique<FakeAudioRecordingBackend>(audioState));
        configuration.captureAudio = false;
        expect(static_cast<bool>(slowSession.start(configuration)));
        auto firstFrame = slowSession.acquireVideoFrame();
        expect(firstFrame.isValid());
        firstFrame.submit();
        expect(videoState->writeEntered.wait(1000));

        std::atomic<bool> acquireReturned { false };
        std::thread acquireThread([&] {
            auto secondFrame = slowSession.acquireVideoFrame();
            acquireReturned.store(true);
            if (secondFrame.isValid()) {
                secondFrame.submit();
            }
        });
        juce::Thread::sleep(20);
        expect(!acquireReturned.load());
        videoState->allowWrite.signal();
        acquireThread.join();
        expect(acquireReturned.load());
        expect(!slowSession.hasFailed());
        slowSession.finish();
        expectEquals(videoState->writes.load(), 2);

        beginTest("Stopping wakes a renderer waiting for the video buffer");
        videoState = std::make_shared<VideoBackendState>();
        videoState->blockWrites = true;
        LiveRecordingSession stoppingSession(std::make_unique<FakeVideoRecordingBackend>(videoState),
                                             std::make_unique<FakeAudioRecordingBackend>(std::make_shared<AudioBackendState>()));
        expect(static_cast<bool>(stoppingSession.start(configuration)));
        auto pendingFrame = stoppingSession.acquireVideoFrame();
        expect(pendingFrame.isValid());
        pendingFrame.submit();
        expect(videoState->writeEntered.wait(1000));

        std::atomic<bool> waitingAcquireReturned { false };
        std::atomic<bool> waitingAcquireWasValid { true };
        std::thread waitingAcquireThread([&] {
            auto frame = stoppingSession.acquireVideoFrame();
            waitingAcquireWasValid.store(frame.isValid());
            waitingAcquireReturned.store(true);
        });
        juce::Thread::sleep(20);
        expect(!waitingAcquireReturned.load());

        std::thread finishWhileBlockedThread([&] { stoppingSession.finish(); });
        for (int attempt = 0; attempt < 100 && !waitingAcquireReturned.load(); ++attempt) {
            juce::Thread::sleep(1);
        }
        expect(waitingAcquireReturned.load());
        expect(!waitingAcquireWasValid.load());
        videoState->allowWrite.signal();
        waitingAcquireThread.join();
        finishWhileBlockedThread.join();
        expectEquals(videoState->writes.load(), 1);

        beginTest("Invalid configurations fail without starting a backend");
        LiveRecordingSession invalidSession(std::make_unique<FakeVideoRecordingBackend>(std::make_shared<VideoBackendState>()),
                                            std::make_unique<FakeAudioRecordingBackend>(std::make_shared<AudioBackendState>()));
        expect(!static_cast<bool>(invalidSession.start({})));
    }
};

static LiveRecordingSessionTest liveRecordingSessionTest;

}
