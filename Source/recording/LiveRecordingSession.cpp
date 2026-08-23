#include "LiveRecordingSession.h"

#include "../audio/AudioRecorder.h"
#include "../video/VideoEncodingConstants.h"
#include <osci_render_core/concurrency/osci_WriteProcess.h>

namespace {

class ProcessVideoRecordingBackend final : public VideoRecordingBackend {
public:
    bool start(const juce::String& command) override {
        return process.start(command);
    }

    bool write(std::span<const std::uint8_t> frame) override {
        return process.write(const_cast<std::uint8_t*>(frame.data()), frame.size(),
                             VideoEncodingConstants::frameWriteTimeoutMs) != 0;
    }

    void finish() override {
        process.close();
    }

private:
    osci::WriteProcess process;
};

class WavAudioRecordingBackend final : public AudioRecordingBackend {
public:
    bool start(const juce::File& file, double sampleRate) override {
        recorder.setSampleRate(sampleRate);
        recorder.startRecording(file);
        return recorder.isRecording();
    }

    void write(const juce::AudioBuffer<float>& buffer) override {
        recorder.audioThreadCallback(buffer);
    }

    void finish() override {
        recorder.stop();
    }

private:
    AudioRecorder recorder;
};

}

class LiveRecordingSession::WriterThread final : public juce::Thread {
public:
    explicit WriterThread(LiveRecordingSession& owner)
        : juce::Thread("Video Recording Writer"), owner(owner) {}

    void run() override {
        while (!threadShouldExit() || owner.videoFramePending.load(std::memory_order_acquire)) {
            if (!owner.videoFramePending.load(std::memory_order_acquire)) {
                owner.videoFrameReady.wait(100);
                continue;
            }

            if (!owner.videoBackend->write(owner.videoFrame)) {
                owner.videoFramePending.store(false, std::memory_order_release);
                owner.videoFrameConsumed.signal();
                owner.fail("An error occurred while writing a video frame to FFmpeg.");
                return;
            }
            owner.videoFramePending.store(false, std::memory_order_release);
            owner.videoFrameConsumed.signal();
        }
    }

private:
    LiveRecordingSession& owner;
};

LiveRecordingSession::VideoFrame::VideoFrame(LiveRecordingSession& newOwner, std::span<std::uint8_t> newBytes)
    : owner(&newOwner), bytes(newBytes) {}

LiveRecordingSession::VideoFrame::VideoFrame(VideoFrame&& other) noexcept
    : owner(std::exchange(other.owner, nullptr)), bytes(std::exchange(other.bytes, {})) {}

LiveRecordingSession::VideoFrame& LiveRecordingSession::VideoFrame::operator=(VideoFrame&& other) noexcept {
    if (this != &other) {
        release(false);
        owner = std::exchange(other.owner, nullptr);
        bytes = std::exchange(other.bytes, {});
    }
    return *this;
}

LiveRecordingSession::VideoFrame::~VideoFrame() {
    release(false);
}

void LiveRecordingSession::VideoFrame::submit() {
    release(true);
}

void LiveRecordingSession::VideoFrame::release(bool submitFrame) {
    if (owner != nullptr) {
        owner->releaseVideoFrame(submitFrame);
        owner = nullptr;
        bytes = {};
    }
}

LiveRecordingSession::LiveRecordingSession()
    : LiveRecordingSession(std::make_unique<ProcessVideoRecordingBackend>(),
                           std::make_unique<WavAudioRecordingBackend>()) {}

LiveRecordingSession::LiveRecordingSession(std::unique_ptr<VideoRecordingBackend> videoBackend,
                                           std::unique_ptr<AudioRecordingBackend> audioBackend)
    : videoBackend(std::move(videoBackend)),
      audioBackend(std::move(audioBackend)),
      writerThread(std::make_unique<WriterThread>(*this)) {}

LiveRecordingSession::~LiveRecordingSession() {
    finish();
}

LiveRecordingResult LiveRecordingSession::start(const LiveRecordingConfiguration& configuration) {
    finish();
    reset();

    if (!configuration.captureVideo && !configuration.captureAudio) {
        return { false, "Recording must capture video, audio, or both." };
    }
    if (configuration.captureVideo
        && (configuration.videoWidth <= 0 || configuration.videoHeight <= 0
            || configuration.buildVideoCommand == nullptr)) {
        return { false, "The video recording configuration is invalid." };
    }
    if (configuration.captureAudio && configuration.sampleRate <= 0.0) {
        return { false, "The audio recording sample rate is invalid." };
    }

    artifacts.audioCodecArgs = configuration.audioCodecArgs;

    if (configuration.captureVideo) {
        artifacts.video = std::make_unique<juce::TemporaryFile>("." + configuration.videoFileExtension);
        const auto command = configuration.buildVideoCommand(artifacts.video->getFile());
        if (command.isEmpty() || !videoBackend->start(command)) {
            reset();
            return { false, "Failed to start the FFmpeg video encoder." };
        }

        const auto frameBytes = static_cast<std::size_t>(configuration.videoWidth)
                              * static_cast<std::size_t>(configuration.videoHeight) * 4;
        videoFrame.resize(frameBytes);
        recordingVideo = true;
        writerThread->startThread();
    }

    if (configuration.captureAudio) {
        artifacts.audio = std::make_unique<juce::TemporaryFile>(".wav");
        if (!audioBackend->start(artifacts.audio->getFile(), configuration.sampleRate)) {
            finish();
            return { false, "Failed to start the audio recorder." };
        }
        recordingAudio = true;
    }

    return {};
}

LiveRecordingSession::VideoFrame LiveRecordingSession::acquireVideoFrame() {
    if (!recordingVideo.load(std::memory_order_acquire) || hasFailed()) {
        return {};
    }

    if (videoFrameAcquired.exchange(true, std::memory_order_acq_rel)) {
        jassertfalse;
        return {};
    }
    if (!recordingVideo.load(std::memory_order_acquire) || hasFailed()) {
        releaseVideoFrame(false);
        return {};
    }

    while (videoFramePending.load(std::memory_order_acquire)) {
        videoFrameConsumed.wait(100);
        if (!recordingVideo.load(std::memory_order_acquire) || hasFailed()) {
            releaseVideoFrame(false);
            return {};
        }
    }

    return VideoFrame(*this, videoFrame);
}

void LiveRecordingSession::releaseVideoFrame(bool submitFrame) {
    if (submitFrame) {
        videoFramePending.store(true, std::memory_order_release);
        videoFrameReady.signal();
    }
    videoFrameAcquired.store(false, std::memory_order_release);
    videoCaptureFinished.signal();
}

void LiveRecordingSession::writeAudioBlock(const juce::AudioBuffer<float>& buffer) {
    if (recordingAudio.load(std::memory_order_acquire) && !hasFailed()) {
        audioBackend->write(buffer);
    }
}

LiveRecordingArtifacts LiveRecordingSession::finish() {
    recordingVideo.store(false, std::memory_order_release);
    recordingAudio.store(false, std::memory_order_release);
    videoFrameConsumed.signal();
    while (videoFrameAcquired.load(std::memory_order_acquire)) {
        videoCaptureFinished.wait(100);
    }
    if (writerThread->isThreadRunning()) {
        writerThread->signalThreadShouldExit();
        videoFrameReady.signal();
        writerThread->stopThread(VideoEncodingConstants::frameWriteTimeoutMs + 1000);
    }
    videoBackend->finish();
    audioBackend->finish();
    videoFramePending.store(false, std::memory_order_release);
    videoFrame.clear();
    return std::move(artifacts);
}

bool LiveRecordingSession::isRecording() const {
    return capturesVideo() || capturesAudio();
}

juce::String LiveRecordingSession::getFailureMessage() const {
    const juce::SpinLock::ScopedLockType lock(failureLock);
    return failureMessage;
}

void LiveRecordingSession::reset() {
    recordingVideo.store(false, std::memory_order_release);
    recordingAudio.store(false, std::memory_order_release);
    videoFrameAcquired.store(false, std::memory_order_release);
    writerFailed.store(false, std::memory_order_release);
    {
        const juce::SpinLock::ScopedLockType lock(failureLock);
        failureMessage.clear();
    }
    videoFramePending.store(false, std::memory_order_release);
    artifacts = {};
}

void LiveRecordingSession::fail(juce::String message) {
    {
        const juce::SpinLock::ScopedLockType lock(failureLock);
        failureMessage = std::move(message);
    }
    writerFailed.store(true, std::memory_order_release);
    videoFrameConsumed.signal();
}
