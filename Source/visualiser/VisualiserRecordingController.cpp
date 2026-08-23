#include "VisualiserRecordingController.h"

VisualiserRecordingController::VisualiserRecordingController(const juce::File& ffmpegExecutable)
    : exporter([this](const juce::File& video, const juce::File& audio, const juce::File& destination,
                      const juce::StringArray& audioCodecArgs, juce::String& error) {
          return encoderManager != nullptr
              && encoderManager->muxAudioAndVideo(video, audio, destination, audioCodecArgs, error);
      }) {
#if OSCI_PREMIUM
    encoderManager = std::make_unique<FFmpegEncoderManager>(ffmpegExecutable);
#else
    juce::ignoreUnused(ffmpegExecutable);
#endif
}

LiveRecordingResult VisualiserRecordingController::start(const VisualiserRecordingConfiguration& configuration) {
    renderSize = configuration.renderSize;
    const auto outputExtension = configuration.captureVideo
        ? (configuration.preserveAlpha ? "mov" : VideoEncodingConstants::getVideoCodecInfo(configuration.codec).defaultFileExtension)
        : "wav";

    if (configuration.captureVideo) {
        const auto requiredCodec = configuration.preserveAlpha ? VideoCodec::ProRes4444 : configuration.codec;
        if (encoderManager == nullptr || !encoderManager->supportsVideoCodec(requiredCodec)) {
            return {
                false,
                configuration.preserveAlpha
                    ? "This FFmpeg installation does not include the ProRes 4444 encoder required for transparent video."
                    : "This FFmpeg installation does not include an encoder for the selected video codec.",
            };
        }
    }

    LiveRecordingConfiguration sessionConfiguration {
        .captureVideo = configuration.captureVideo,
        .captureAudio = configuration.captureAudio,
        .videoWidth = configuration.renderSize.width,
        .videoHeight = configuration.renderSize.height,
        .sampleRate = configuration.sampleRate,
        .videoFileExtension = outputExtension,
        .audioCodecArgs = configuration.audioCodecArgs,
    };
    sessionConfiguration.buildVideoCommand = [this, configuration](const juce::File& outputFile) {
        return encoderManager->buildVideoEncodingCommand(
            configuration.codec,
            configuration.crf,
            configuration.renderSize.width,
            configuration.renderSize.height,
            configuration.frameRate,
            configuration.compressionPreset,
            outputFile,
            configuration.preserveAlpha);
    };
    return session.start(sessionConfiguration);
}

LiveRecordingSession::VideoFrame VisualiserRecordingController::acquireVideoFrame(VisualiserRenderSize renderedSize) {
    if (renderedSize != renderSize) {
        return {};
    }
    return session.acquireVideoFrame();
}

void VisualiserRecordingController::writeAudioBlock(const juce::AudioBuffer<float>& buffer) {
    session.writeAudioBlock(buffer);
}

LiveRecordingArtifacts VisualiserRecordingController::finish() {
    return session.finish();
}

RecordingExportResult VisualiserRecordingController::exportRecording(const LiveRecordingArtifacts& artifacts,
                                                                      const juce::File& destination) const {
    return exporter.exportRecording(artifacts, destination);
}
