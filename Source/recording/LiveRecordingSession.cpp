#include "LiveRecordingSession.h"

#include "../audio/AudioRecorder.h"
#include <osci_render_core/concurrency/osci_WriteProcess.h>

namespace {

class ProcessVideoRecordingBackend final : public VideoRecordingBackend {
public:
    bool start(const juce::String& command) override {
        return process.start(command);
    }

    bool write(std::span<const std::uint8_t> frame) override {
        return process.write(const_cast<std::uint8_t*>(frame.data()), frame.size()) != 0;
    }

    void finish() override {
        process.close();
    }

    bool isRunning() override {
        return process.isRunning();
    }

private:
    osci::WriteProcess process;
};

class WavAudioRecordingBackend final : public AudioRecordingBackend {
public:
    void setSampleRate(double sampleRate) override {
        recorder.setSampleRate(sampleRate);
    }

    bool start(const juce::File& file) override {
        recorder.startRecording(file);
        return recorder.isRecording();
    }

    void write(const juce::AudioBuffer<float>& buffer) override {
        recorder.audioThreadCallback(buffer);
    }

    void finish() override {
        recorder.stop();
    }

    bool isRunning() const override {
        return recorder.isRecording();
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
        while (!threadShouldExit()
               || owner.queuedFrameRead.load(std::memory_order_relaxed)
                    < owner.queuedFrameWrite.load(std::memory_order_acquire)) {
            const auto read = owner.queuedFrameRead.load(std::memory_order_relaxed);
            if (read >= owner.queuedFrameWrite.load(std::memory_order_acquire)) {
                owner.writerWakeEvent.wait(100);
                continue;
            }

            const auto slot = static_cast<std::size_t>(read % LiveRecordingSession::frameQueueCapacity);
            const auto& frame = owner.videoFrames[slot];
            if (!owner.videoBackend->write(std::span<const std::uint8_t>(frame))) {
                owner.queuedFrameRead.store(read + 1, std::memory_order_release);
                owner.fail("An error occurred while writing a video frame to FFmpeg.");
                return;
            }
            owner.queuedFrameRead.store(read + 1, std::memory_order_release);
        }
    }

private:
    LiveRecordingSession& owner;
};

std::unique_ptr<VideoRecordingBackend> createVideoRecordingBackend() {
    return std::make_unique<ProcessVideoRecordingBackend>();
}

std::unique_ptr<AudioRecordingBackend> createAudioRecordingBackend() {
    return std::make_unique<WavAudioRecordingBackend>();
}

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

    artifacts.hasVideo = configuration.captureVideo;
    artifacts.hasAudio = configuration.captureAudio;
    artifacts.fileExtension = configuration.captureVideo ? configuration.videoFileExtension : "wav";
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
        for (auto& frame : videoFrames) {
            frame.resize(frameBytes);
        }
        recordingVideo = true;
        writerThread->startThread();
    }

    if (configuration.captureAudio) {
        artifacts.audio = std::make_unique<juce::TemporaryFile>(".wav");
        audioBackend->setSampleRate(configuration.sampleRate);
        if (!audioBackend->start(artifacts.audio->getFile())) {
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

    activeVideoCaptures.fetch_add(1, std::memory_order_acq_rel);
    if (!recordingVideo.load(std::memory_order_acquire) || hasFailed()) {
        releaseVideoFrame(false);
        return {};
    }

    jassert(pendingWriteSlot < 0);
    if (pendingWriteSlot >= 0) {
        releaseVideoFrame(false);
        return {};
    }

    const auto write = queuedFrameWrite.load(std::memory_order_relaxed);
    const auto read = queuedFrameRead.load(std::memory_order_acquire);
    if (write - read >= frameQueueCapacity) {
        fail("The video encoder could not keep up with the rendered frames.");
        releaseVideoFrame(false);
        return {};
    }
    pendingWriteSlot = static_cast<int>(write % frameQueueCapacity);
    return VideoFrame(*this, videoFrames[static_cast<std::size_t>(pendingWriteSlot)]);
}

void LiveRecordingSession::releaseVideoFrame(bool submitFrame) {
    if (submitFrame && pendingWriteSlot >= 0) {
        queuedFrameWrite.fetch_add(1, std::memory_order_release);
        writerWakeEvent.signal();
    }
    pendingWriteSlot = -1;
    if (activeVideoCaptures.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        videoCaptureFinished.signal();
    }
}

void LiveRecordingSession::writeAudioBlock(const juce::AudioBuffer<float>& buffer) {
    if (recordingAudio.load(std::memory_order_acquire) && !hasFailed()) {
        audioBackend->write(buffer);
    }
}

LiveRecordingArtifacts LiveRecordingSession::finish() {
    const bool wasRecordingVideo = recordingVideo.exchange(false, std::memory_order_acq_rel);
    const bool wasRecordingAudio = recordingAudio.exchange(false, std::memory_order_acq_rel);
    while (activeVideoCaptures.load(std::memory_order_acquire) > 0) {
        videoCaptureFinished.wait(100);
    }
    if (writerThread->isThreadRunning()) {
        writerThread->signalThreadShouldExit();
        writerWakeEvent.signal();
        writerThread->stopThread(10000);
    }
    if (wasRecordingVideo || videoBackend->isRunning()) {
        videoBackend->finish();
    }
    if (wasRecordingAudio || audioBackend->isRunning()) {
        audioBackend->finish();
    }
    queuedFrameWrite.store(0, std::memory_order_release);
    queuedFrameRead.store(0, std::memory_order_release);
    for (auto& frame : videoFrames) {
        frame.clear();
    }
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
    pendingWriteSlot = -1;
    activeVideoCaptures.store(0, std::memory_order_release);
    writerFailed.store(false, std::memory_order_release);
    {
        const juce::SpinLock::ScopedLockType lock(failureLock);
        failureMessage.clear();
    }
    queuedFrameWrite.store(0, std::memory_order_release);
    queuedFrameRead.store(0, std::memory_order_release);
    artifacts = {};
}

void LiveRecordingSession::fail(juce::String message) {
    {
        const juce::SpinLock::ScopedLockType lock(failureLock);
        failureMessage = std::move(message);
    }
    writerFailed.store(true, std::memory_order_release);
}
