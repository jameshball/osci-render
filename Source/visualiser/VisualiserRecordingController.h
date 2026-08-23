#pragma once

#include "../recording/LiveRecordingSession.h"
#include "../recording/RecordingExporter.h"
#include "../video/FFmpegEncoderManager.h"
#include "RecordingSettings.h"
#include <osci_gui/visualiser/osci_VisualiserGeometry.h>

class VisualiserRecordingController final {
public:
    using ExportCompletion = std::function<void(RecordingExportResult result, juce::File destination)>;

    explicit VisualiserRecordingController(const juce::File& ffmpegExecutable);

    [[nodiscard]] bool wantsVideo(RecordingSettings& settings) const;
    LiveRecordingResult start(RecordingSettings& settings, double sampleRate);
    LiveRecordingResult stopAndChooseExport(const juce::File& destinationDirectory, const juce::String& fileNamePrefix, ExportCompletion completion);
    void discard();

    LiveRecordingSession::VideoFrame acquireVideoFrame(VisualiserRenderSize renderedSize);
    void writeAudioBlock(const juce::AudioBuffer<float>& buffer);

    bool isRecording() const { return session.isRecording(); }
    bool capturesVideo() const { return session.capturesVideo(); }
    bool capturesAudio() const { return session.capturesAudio(); }
    bool hasFailed() const { return session.hasFailed(); }
    juce::String getFailureMessage() const { return session.getFailureMessage(); }

private:
    std::unique_ptr<FFmpegEncoderManager> encoderManager;
    LiveRecordingSession session;
    RecordingExporter exporter;
    juce::ThreadPool exportThreadPool { juce::ThreadPool::Options().withNumberOfThreads(1).withThreadName("Recording Export") };
    VisualiserRenderSize renderSize;
    std::unique_ptr<juce::FileChooser> chooser;
};
