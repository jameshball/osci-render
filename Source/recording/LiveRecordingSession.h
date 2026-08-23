#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

struct LiveRecordingConfiguration {
    bool captureVideo = false;
    bool captureAudio = false;
    int videoWidth = 0;
    int videoHeight = 0;
    double sampleRate = 0.0;
    juce::String videoFileExtension;
    juce::StringArray audioCodecArgs;
    std::function<juce::String(const juce::File&)> buildVideoCommand;
};

struct LiveRecordingResult {
    bool succeeded = true;
    juce::String message;

    explicit operator bool() const { return succeeded; }
};

struct LiveRecordingArtifacts {
    juce::StringArray audioCodecArgs;
    std::unique_ptr<juce::TemporaryFile> video;
    std::unique_ptr<juce::TemporaryFile> audio;

    [[nodiscard]] bool hasVideo() const { return video != nullptr; }
    [[nodiscard]] bool hasAudio() const { return audio != nullptr; }
    [[nodiscard]] juce::String getOutputFileExtension() const {
        const auto* output = hasVideo() ? video.get() : audio.get();
        return output != nullptr ? output->getFile().getFileExtension().trimCharactersAtStart(".") : juce::String {};
    }
};

class VideoRecordingBackend {
public:
    virtual ~VideoRecordingBackend() = default;
    virtual bool start(const juce::String& command) = 0;
    virtual bool write(std::span<const std::uint8_t> frame) = 0;
    virtual void finish() = 0;
};

class AudioRecordingBackend {
public:
    virtual ~AudioRecordingBackend() = default;
    virtual bool start(const juce::File& file, double sampleRate) = 0;
    virtual void write(const juce::AudioBuffer<float>& buffer) = 0;
    virtual void finish() = 0;
};

class LiveRecordingSession {
public:
    class VideoFrame final {
    public:
        VideoFrame() = default;
        VideoFrame(VideoFrame&& other) noexcept;
        VideoFrame& operator=(VideoFrame&& other) noexcept;
        ~VideoFrame();

        [[nodiscard]] std::span<std::uint8_t> getBytes() const { return bytes; }
        [[nodiscard]] bool isValid() const { return owner != nullptr; }
        void submit();

    private:
        friend class LiveRecordingSession;
        VideoFrame(LiveRecordingSession& owner, std::span<std::uint8_t> bytes);
        void release(bool submitFrame);

        LiveRecordingSession* owner = nullptr;
        std::span<std::uint8_t> bytes;
    };

    LiveRecordingSession();
    LiveRecordingSession(std::unique_ptr<VideoRecordingBackend> videoBackend,
                         std::unique_ptr<AudioRecordingBackend> audioBackend);
    ~LiveRecordingSession();

    LiveRecordingResult start(const LiveRecordingConfiguration& configuration);
    VideoFrame acquireVideoFrame();
    void writeAudioBlock(const juce::AudioBuffer<float>& buffer);
    LiveRecordingArtifacts finish();

    bool isRecording() const;
    bool capturesVideo() const { return recordingVideo.load(std::memory_order_acquire); }
    bool capturesAudio() const { return recordingAudio.load(std::memory_order_acquire); }
    bool hasFailed() const { return writerFailed.load(std::memory_order_acquire); }
    juce::String getFailureMessage() const;

private:
    class WriterThread;

    void reset();
    void fail(juce::String message);
    void releaseVideoFrame(bool submitFrame);

    std::unique_ptr<VideoRecordingBackend> videoBackend;
    std::unique_ptr<AudioRecordingBackend> audioBackend;
    std::unique_ptr<WriterThread> writerThread;
    std::vector<std::uint8_t> videoFrame;
    juce::WaitableEvent videoFrameReady;
    juce::WaitableEvent videoFrameConsumed;
    std::atomic<bool> videoFramePending { false };
    std::atomic<int> activeVideoCaptures { 0 };
    juce::WaitableEvent videoCaptureFinished;
    std::atomic<bool> recordingVideo { false };
    std::atomic<bool> recordingAudio { false };
    std::atomic<bool> writerFailed { false };
    mutable juce::SpinLock failureLock;
    juce::String failureMessage;
    LiveRecordingArtifacts artifacts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveRecordingSession)
};
