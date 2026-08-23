#pragma once

#include "LiveRecordingSession.h"

#include <functional>

struct RecordingExportResult {
    bool succeeded = true;
    juce::String message;

    explicit operator bool() const { return succeeded; }
};

class RecordingExporter final {
public:
    using MuxAudioAndVideo = std::function<bool(const juce::File& video, const juce::File& audio,
                                                const juce::File& destination,
                                                const juce::StringArray& audioCodecArgs,
                                                juce::String& error)>;

    explicit RecordingExporter(MuxAudioAndVideo muxAudioAndVideo);

    RecordingExportResult exportRecording(const LiveRecordingArtifacts& artifacts,
                                            const juce::File& destination) const;

private:
    MuxAudioAndVideo muxAudioAndVideo;
};
