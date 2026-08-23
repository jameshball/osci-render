#include "RecordingExporter.h"

RecordingExporter::RecordingExporter(MuxAudioAndVideo muxAudioAndVideo)
    : muxAudioAndVideo(std::move(muxAudioAndVideo)) {}

RecordingExportResult RecordingExporter::exportRecording(const LiveRecordingArtifacts& artifacts,
                                                           const juce::File& destination) const {
    if (destination == juce::File()) {
        return { false, "No recording destination was selected." };
    }

    bool exported = false;
    juce::String error;
    if (artifacts.hasAudio() && artifacts.hasVideo()) {
        if (muxAudioAndVideo == nullptr) {
            return { false, "The recording artifacts required for muxing are unavailable." };
        }
        exported = muxAudioAndVideo(artifacts.video->getFile(), artifacts.audio->getFile(), destination,
                                    artifacts.audioCodecArgs, error);
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
