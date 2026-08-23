#include "VisualiserRecordingController.h"

VisualiserRecordingController::VisualiserRecordingController(const juce::File& ffmpegExecutable)
#if OSCI_PREMIUM
    : encoderManager(std::make_unique<FFmpegEncoderManager>(ffmpegExecutable)),
      exporter(*encoderManager) {
#else
    {
    juce::ignoreUnused(ffmpegExecutable);
#endif
}

bool VisualiserRecordingController::wantsVideo(RecordingSettings& settings) const {
#if OSCI_PREMIUM
    return settings.recordingVideo();
#else
    juce::ignoreUnused(settings);
    return false;
#endif
}

LiveRecordingResult VisualiserRecordingController::start(RecordingSettings& settings, double sampleRate) {
#if OSCI_PREMIUM
    const bool captureVideo = settings.recordingVideo();
    const auto encodingConfiguration = settings.createVideoEncodingConfiguration();
    const bool captureAudio = encodingConfiguration.includeAudio;
    const bool preserveAlpha = captureVideo && encodingConfiguration.preserveAlpha;
    renderSize = encodingConfiguration.renderSize;
#else
    juce::ignoreUnused(settings);
    const VideoEncodingConfiguration encodingConfiguration;
    const bool captureVideo = false;
    const bool captureAudio = true;
    const bool preserveAlpha = false;
    renderSize = {};
#endif

    if (!captureVideo && !captureAudio) {
        return { false, "Recording must capture video, audio, or both." };
    }

    const auto outputExtension = captureVideo ? encodingConfiguration.fileExtension : "wav";

    if (captureVideo) {
        if (encoderManager == nullptr || !encoderManager->supportsVideoCodec(encodingConfiguration.codec)) {
            return {
                false,
                preserveAlpha
                    ? "This FFmpeg installation does not include the ProRes 4444 encoder required for transparent video."
                    : "This FFmpeg installation does not include an encoder for the selected video codec.",
            };
        }
    }

    LiveRecordingConfiguration sessionConfiguration {
        .captureVideo = captureVideo,
        .captureAudio = captureAudio,
        .videoWidth = renderSize.width,
        .videoHeight = renderSize.height,
        .sampleRate = sampleRate,
        .videoFileExtension = outputExtension,
        .audioCodecArgs = encodingConfiguration.audioCodecArgs,
    };
    sessionConfiguration.buildVideoCommand = [this, encodingConfiguration, preserveAlpha](const juce::File& outputFile) {
        return encoderManager->buildVideoEncodingCommand(
            encodingConfiguration.codec,
            encodingConfiguration.crf,
            renderSize.width,
            renderSize.height,
            encodingConfiguration.frameRate,
            encodingConfiguration.compressionPreset,
            outputFile,
            preserveAlpha);
    };
    return session.start(sessionConfiguration);
}

LiveRecordingResult VisualiserRecordingController::stopAndChooseExport(const juce::File& destinationDirectory, const juce::String& fileNamePrefix, ExportCompletion completion) {
    if (!session.isRecording()) {
        return { false, "There is no active recording to stop." };
    }

    jassert(completion != nullptr);
    const bool failed = session.hasFailed();
    const auto failureMessage = session.getFailureMessage();
    auto artifacts = std::make_shared<LiveRecordingArtifacts>(session.finish());
    if (failed) {
        return { false, failureMessage };
    }

    const auto extension = artifacts->getOutputFileExtension();
    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
    const auto suggestedFile = destinationDirectory.getChildFile(fileNamePrefix + "_" + timestamp + "." + extension);
    chooser = std::make_unique<juce::FileChooser>("Save recording", suggestedFile, "*." + extension);
    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [this, artifacts, extension, completion = std::move(completion)](const juce::FileChooser& fileChooser) {
        auto destination = fileChooser.getResult();
        if (destination == juce::File()) {
            return;
        }
        if (!destination.hasFileExtension(extension)) {
            destination = destination.withFileExtension(extension);
        }
        exportThreadPool.addJob([this, artifacts, destination, completion = std::move(completion)] {
            auto result = exporter.exportRecording(*artifacts, destination);
            juce::MessageManager::callAsync([result = std::move(result), destination, completion = std::move(completion)]() mutable {
                if (completion != nullptr) {
                    completion(std::move(result), std::move(destination));
                }
            });
        });
    });
    return {};
}

void VisualiserRecordingController::discard() {
    session.finish();
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
