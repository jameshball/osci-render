#include "RecordingExporter.h"
#include "../video/FFmpegEncoderManager.h"

RecordingExporter::RecordingExporter(FFmpegEncoderManager& encoderManager)
    : encoderManager(&encoderManager) {}

RecordingExportResult RecordingExporter::exportRecording(const LiveRecordingArtifacts& artifacts,
                                                           const juce::File& destination) const {
    if (destination == juce::File()) {
        return { false, "No recording destination was selected." };
    }

    bool exported = false;
    juce::String error;
    if (artifacts.hasAudio() && artifacts.hasVideo()) {
        if (encoderManager == nullptr) {
            return { false, "The video encoder required for muxing is unavailable." };
        }
        exported = encoderManager->muxAudioAndVideo(artifacts.video->getFile(), artifacts.audio->getFile(),
                                                     destination, artifacts.audioCodecArgs, error);
    } else if (artifacts.hasAudio()) {
        exported = artifacts.audio->getFile().copyFileTo(destination);
    } else if (artifacts.hasVideo()) {
        exported = artifacts.video->getFile().copyFileTo(destination);
    } else {
        return { false, "The recording did not produce any media." };
    }

    if (!exported) {
        if (error.isEmpty()) {
            error = "Could not write the recording to " + destination.getFullPathName();
        }
        return { false, error };
    }
    return {};
}
