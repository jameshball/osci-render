#pragma once

#include "LiveRecordingSession.h"

class FFmpegEncoderManager;

struct RecordingExportResult {
    bool succeeded = true;
    juce::String message;

    explicit operator bool() const { return succeeded; }
};

enum class RecordingExportAction {
    noMedia,
    copyAudio,
    copyVideo,
    muxAudioAndVideo,
};

class RecordingExporter final {
public:
    RecordingExporter() = default;
    explicit RecordingExporter(FFmpegEncoderManager& encoderManager);

    RecordingExportResult exportRecording(const LiveRecordingArtifacts& artifacts,
                                            const juce::File& destination) const;
    static RecordingExportAction getExportAction(const LiveRecordingArtifacts& artifacts);

private:
    FFmpegEncoderManager* encoderManager = nullptr;
};
