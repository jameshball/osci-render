#include "RecordingExporter.h"
#include "../video/FFmpegEncoderManager.h"

RecordingExporter::RecordingExporter(FFmpegEncoderManager& encoderManager)
    : encoderManager(&encoderManager) {}

RecordingExportAction RecordingExporter::getExportAction(const LiveRecordingArtifacts& artifacts) {
    if (artifacts.hasAudio() && artifacts.hasVideo()) {
        return RecordingExportAction::muxAudioAndVideo;
    }
    if (artifacts.hasAudio()) {
        return RecordingExportAction::copyAudio;
    }
    if (artifacts.hasVideo()) {
        return RecordingExportAction::copyVideo;
    }
    return RecordingExportAction::noMedia;
}

RecordingExportResult RecordingExporter::exportRecording(const LiveRecordingArtifacts& artifacts,
                                                           const juce::File& destination) const {
    if (destination == juce::File()) {
        return { false, "No recording destination was selected." };
    }

    bool exported = false;
    juce::String error;
    switch (getExportAction(artifacts)) {
        case RecordingExportAction::muxAudioAndVideo:
            if (encoderManager == nullptr) {
                return { false, "The video encoder required for muxing is unavailable." };
            }
            exported = encoderManager->muxAudioAndVideo(artifacts.video->getFile(), artifacts.audio->getFile(),
                                                         destination, artifacts.audioCodecArgs, error);
            break;
        case RecordingExportAction::copyAudio:
            exported = artifacts.audio->getFile().copyFileTo(destination);
            break;
        case RecordingExportAction::copyVideo:
            exported = artifacts.video->getFile().copyFileTo(destination);
            break;
        case RecordingExportAction::noMedia:
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
