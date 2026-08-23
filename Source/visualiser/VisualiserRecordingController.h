#pragma once

#include "../recording/LiveRecordingSession.h"
#include "../recording/RecordingExporter.h"
#include "../video/FFmpegEncoderManager.h"
#include <osci_gui/visualiser/osci_VisualiserGeometry.h>

struct VisualiserRecordingConfiguration {
    bool captureVideo = false;
    bool captureAudio = false;
    bool preserveAlpha = false;
    VideoCodec codec = VideoCodec::H264;
    VisualiserRenderSize renderSize;
    double frameRate = 60.0;
    double sampleRate = 0.0;
    int crf = 20;
    juce::String compressionPreset;
    juce::StringArray audioCodecArgs;
};

class VisualiserRecordingController final {
public:
    explicit VisualiserRecordingController(const juce::File& ffmpegExecutable);

    LiveRecordingResult start(const VisualiserRecordingConfiguration& configuration);
    LiveRecordingSession::VideoFrame acquireVideoFrame(VisualiserRenderSize renderedSize);
    void writeAudioBlock(const juce::AudioBuffer<float>& buffer);
    LiveRecordingArtifacts finish();
    RecordingExportResult exportRecording(const LiveRecordingArtifacts& artifacts,
                                            const juce::File& destination) const;

    bool isRecording() const { return session.isRecording(); }
    bool capturesVideo() const { return session.capturesVideo(); }
    bool capturesAudio() const { return session.capturesAudio(); }
    bool hasFailed() const { return session.hasFailed(); }
    juce::String getFailureMessage() const { return session.getFailureMessage(); }

private:
    std::unique_ptr<FFmpegEncoderManager> encoderManager;
    LiveRecordingSession session;
    RecordingExporter exporter;
    VisualiserRenderSize renderSize;
};
