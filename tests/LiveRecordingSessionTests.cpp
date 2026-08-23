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
    bool isRunning() override { return state->running; }

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

    void setSampleRate(double newSampleRate) override { sampleRate = newSampleRate; }
    bool start(const juce::File&) override {
        state->running = sampleRate > 0.0;
        return state->running;
    }
    void write(const juce::AudioBuffer<float>&) override { ++state->writes; }
    void finish() override { state->running = false; }
    bool isRunning() const override { return state->running; }

private:
    std::shared_ptr<AudioBackendState> state;
    double sampleRate = 0.0;
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

        beginTest("A full bounded queue stops an encoder that cannot keep up");
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
        auto secondFrame = slowSession.acquireVideoFrame();
        expect(secondFrame.isValid());
        secondFrame.submit();
        auto thirdFrame = slowSession.acquireVideoFrame();
        expect(thirdFrame.isValid());
        thirdFrame.submit();
        expect(!slowSession.acquireVideoFrame().isValid());
        expect(slowSession.hasFailed());
        videoState->allowWrite.signal();
        slowSession.finish();

        beginTest("Invalid configurations fail without starting a backend");
        LiveRecordingSession invalidSession(std::make_unique<FakeVideoRecordingBackend>(std::make_shared<VideoBackendState>()),
                                            std::make_unique<FakeAudioRecordingBackend>(std::make_shared<AudioBackendState>()));
        expect(!static_cast<bool>(invalidSession.start({})));
    }
};

static LiveRecordingSessionTest liveRecordingSessionTest;

}
