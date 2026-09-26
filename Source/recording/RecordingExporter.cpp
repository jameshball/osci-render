#include "RecordingExporter.h"
#include "../video/FFmpegEncoderManager.h"

RecordingExporter::RecordingExporter(FFmpegEncoderManager& encoderManager)
    : encoderManager(&encoderManager) {}

RecordingExportResult RecordingExporter::exportRecording(const LiveRecordingArtifacts& artifacts,
                                                           const juce::File& destination) const {
    if (destination == juce::File()) {
        return { false, "No recording destination was selected." };
    }

    juce::TemporaryFile pendingOutput(destination);
    const auto& outputFile = pendingOutput.getFile();
    bool exported = false;
    juce::String error;
    if (artifacts.hasAudio() && artifacts.hasVideo()) {
        if (encoderManager == nullptr) {
            return { false, "The video encoder required for muxing is unavailable." };
        }
        exported = encoderManager->muxAudioAndVideo(artifacts.video->getFile(), artifacts.audio->getFile(),
                                                     outputFile, artifacts.audioCodecArgs, error);
    } else if (artifacts.hasAudio()) {
        exported = artifacts.audio->getFile().copyFileTo(outputFile);
    } else if (artifacts.hasVideo()) {
        exported = artifacts.video->getFile().copyFileTo(outputFile);
    } else {
        return { false, "The recording did not produce any media." };
    }

    if (!exported || !pendingOutput.overwriteTargetFileWithTemporary()) {
        const auto stage = exported ? "final file replacement" : (artifacts.hasAudio() && artifacts.hasVideo() ? "audio/video mux" : "file copy");
        juce::Logger::writeToLog("Recording export diagnostics: stage=" + juce::String(stage)
            + ", audioBytes=" + juce::String(artifacts.hasAudio() ? artifacts.audio->getFile().getSize() : 0)
            + ", videoBytes=" + juce::String(artifacts.hasVideo() ? artifacts.video->getFile().getSize() : 0)
            + ", outputBytes=" + juce::String(outputFile.getSize())
            + ", destinationFreeBytes=" + juce::String(destination.getParentDirectory().getBytesFreeOnVolume())
            + ", tempFreeBytes=" + juce::String(juce::File::getSpecialLocation(juce::File::tempDirectory).getBytesFreeOnVolume())
            + ", destinationFolderExists=" + juce::String(destination.getParentDirectory().isDirectory() ? "yes" : "no"));
        if (error.isEmpty()) {
            error = "Could not write the recording to " + destination.getFullPathName();
        }
        return { false, error };
    }
    return {};
}
