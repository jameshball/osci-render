#include "OfflineAudioToVideoRenderer.h"

#if OSCI_PREMIUM

#include "../LookAndFeel.h"
#include "../logging/WorkflowLogger.h"
#include "../video/VideoEncodingConstants.h"
#include "VisualiserTextureAssets.h"

namespace {

const auto& offlineRenderLog = osci::WorkflowLoggers::offlineAudioToVideo;

juce::String renderModeToString(VisualiserRenderer::RenderMode mode) {
    switch (mode) {
        case VisualiserRenderer::RenderMode::XY:
            return "XY";
        case VisualiserRenderer::RenderMode::XYZ:
            return "XYZ";
        case VisualiserRenderer::RenderMode::XYRGB:
            return "XYRGB";
        default:
            return "unknown";
    }
}

juce::String videoCodecToString(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::H264:
            return "H264";
        case VideoCodec::H265:
            return "H265";
        case VideoCodec::VP9:
            return "VP9";
        case VideoCodec::ProRes:
            return "ProRes 422 HQ";
        case VideoCodec::ProRes4444:
            return "ProRes 4444";
        default:
            return "unknown";
    }
}

juce::String yesNo(bool value) {
    return value ? "yes" : "no";
}

juce::String fileSummary(const juce::File& file) {
    return file.getFileName() + " (" + juce::String((juce::int64) file.getSize()) + " bytes)";
}

juce::String durationSummary(double seconds) {
    return juce::String(seconds, 2) + "s";
}

}

OfflineAudioToVideoRendererComponent::OfflinePreviewRenderer::OfflinePreviewRenderer(
    VisualiserParameters& parameters,
    osci::AudioBackgroundThreadManager& manager,
    juce::WaitableEvent& glReadyEventToSignal)
    : VisualiserRenderer(parameters, manager, {1024, 1024}, 60.0, "Offline"),
      glReadyEvent(glReadyEventToSignal)
{
    setAssets(createVisualiserTextureAssets());
}

void OfflineAudioToVideoRendererComponent::OfflinePreviewRenderer::newOpenGLContextCreated()
{
    VisualiserRenderer::newOpenGLContextCreated();
    glReadyEvent.signal();
    offlineRenderLog.event("offline preview OpenGL context created");
}

OfflineAudioToVideoRendererComponent::WorkerThread::WorkerThread(OfflineAudioToVideoRendererComponent& owner)
    : juce::Thread("OfflineAudioToVideoWorker"), owner(owner)
{
}

void OfflineAudioToVideoRendererComponent::WorkerThread::run()
{
    auto result = owner.renderToFile();
    owner.finishAsync(result);
}

OfflineAudioToVideoRendererComponent::OfflineAudioToVideoRendererComponent(CommonAudioProcessor& processor,
                                                                          VisualiserParameters& visualiserParameters,
                                                                          osci::AudioBackgroundThreadManager& threadManager,
                                                                          RecordingSettings& recordingSettings,
                                                                          const juce::File& inputAudioFile,
                                                                          const juce::File& outputVideoFile,
                                                                          VisualiserRenderer::RenderMode initialRenderMode,
                                                                          bool preserveAlpha)
    : processor(processor),
      recordingSettings(recordingSettings),
      inputAudioFile(inputAudioFile),
      outputVideoFile(outputVideoFile),
      preview(visualiserParameters, threadManager, glReadyEvent),
      initialRenderMode(initialRenderMode),
      preserveAlpha(preserveAlpha)
{
    setOpaque(false);

    addAndMakeVisible(preview);
    addAndMakeVisible(progressBar);
    addAndMakeVisible(cancelButton);

    preview.setRenderMode(initialRenderMode);

    cancelButton.onClick = [this] { cancel(); };

    // Capture frames on the OpenGL renderer thread (same approach as VisualiserComponent).
    preview.setPostRenderCallback([this] {
        const auto tex = preview.getRenderTexture();
        const size_t requiredBytes = (size_t) (4 * tex.width * tex.height);

        {
            const juce::ScopedLock lock(frameLock);
            framePixels.resize(requiredBytes);
            preview.getFrame(framePixels);
            capturedFrameCount++;
        }
    });
}

OfflineAudioToVideoRendererComponent::~OfflineAudioToVideoRendererComponent()
{
    cancel();
    if (worker != nullptr)
        worker->stopThread(2000);
}

void OfflineAudioToVideoRendererComponent::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void OfflineAudioToVideoRendererComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto bottom = area.removeFromBottom(40);
    auto progressRow = bottom;
    auto cancelArea = progressRow.removeFromRight(120);
    progressRow.removeFromRight(10); // padding between progress and cancel

    cancelButton.setBounds(cancelArea);
    progressBar.setBounds(progressRow.withHeight(20).withY(bottom.getY() + 10));

    area.removeFromBottom(12);

    preview.setBounds(area);
}

void OfflineAudioToVideoRendererComponent::start()
{
    if (worker != nullptr)
    {
        offlineRenderLog.event("start ignored because worker is already running");
        return;
    }

    cancelRequested.store(false);
    progressValue = 0.0;
    lastPostedProgressPercent.store(0);
    capturedFrameCount.store(0);
    setProgressAsync(0.0);

    worker = std::make_unique<WorkerThread>(*this);
    worker->startThread();
    offlineRenderLog.event("worker started");
}

void OfflineAudioToVideoRendererComponent::cancel()
{
    const bool wasAlreadyCancelling = cancelRequested.exchange(true);
    if (!wasAlreadyCancelling && worker != nullptr && worker->isThreadRunning())
    {
        offlineRenderLog.event("cancel requested");
    }

    cancelButton.setEnabled(false);

    // Unblock any waits so the worker thread can exit quickly.
    glReadyEvent.signal();
    preview.stopTask();
    if (worker != nullptr)
        worker->signalThreadShouldExit();
}

void OfflineAudioToVideoRendererComponent::setProgressAsync(double newProgress)
{
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<OfflineAudioToVideoRendererComponent>(this), newProgress] {
        if (safeThis == nullptr)
            return;

        safeThis->progressValue = juce::jlimit(0.0, 1.0, newProgress);
    });
}

void OfflineAudioToVideoRendererComponent::finishAsync(Result r)
{
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<OfflineAudioToVideoRendererComponent>(this), r] {
        if (safeThis == nullptr)
            return;

        safeThis->cancelButton.setEnabled(true);

        if (safeThis->onFinished != nullptr)
            safeThis->onFinished(r);
    });
}

OfflineAudioToVideoRendererComponent::Result OfflineAudioToVideoRendererComponent::renderToFile()
{
    Result result;
    const double renderStartedMs = juce::Time::getMillisecondCounterHiRes();

    auto shouldCancel = [&]() {
        return cancelRequested.load() || juce::Thread::currentThreadShouldExit();
    };

    if (shouldCancel())
    {
        offlineRenderLog.event("render cancelled before start");
        result.cancelled = true;
        return result;
    }

    if (!inputAudioFile.existsAsFile())
    {
        offlineRenderLog.event("render failed: input audio file not found: " + inputAudioFile.getFullPathName());
        result.errorMessage = "Input audio file not found.";
        return result;
    }

    const double fps = recordingSettings.getFrameRate();
    const auto renderSize = recordingSettings.getCanvasSize();
    const auto codec = recordingSettings.getVideoCodec();
    const int crf = recordingSettings.getCRF();
    const auto preset = recordingSettings.getCompressionPreset();
    const bool includeAudio = recordingSettings.recordingAudio();

    offlineRenderLog.event(
        "render starting: input=" + fileSummary(inputAudioFile)
        + ", output=" + outputVideoFile.getFileName()
        + ", canvas=" + juce::String(renderSize.width) + "x" + juce::String(renderSize.height)
        + ", fps=" + juce::String(fps, 2)
        + ", codec=" + videoCodecToString(codec)
        + ", crf=" + juce::String(crf)
        + ", preset=" + preset
        + ", includeAudio=" + yesNo(includeAudio)
        + ", preserveAlpha=" + yesNo(preserveAlpha)
        + ", initialMode=" + renderModeToString(initialRenderMode));

    // Wait for OpenGL to be ready so runTask() doesn't stall indefinitely.
    // Poll so cancellation can interrupt promptly.
    {
        bool ready = false;
        const double glWaitStartedMs = juce::Time::getMillisecondCounterHiRes();
        offlineRenderLog.event("waiting for offline preview OpenGL context");
        for (int i = 0; i < 80; ++i)
        {
            if (shouldCancel())
            {
                offlineRenderLog.event("render cancelled while waiting for OpenGL context");
                result.cancelled = true;
                return result;
            }

            if (glReadyEvent.wait(100))
            {
                ready = true;
                break;
            }
        }

        if (!ready)
        {
            const double waitedMs = juce::Time::getMillisecondCounterHiRes() - glWaitStartedMs;
            offlineRenderLog.event("render failed: OpenGL context not ready after " + juce::String(waitedMs, 0) + " ms");
            result.errorMessage = "Visualiser could not initialise OpenGL.";
            return result;
        }

        const double waitedMs = juce::Time::getMillisecondCounterHiRes() - glWaitStartedMs;
        offlineRenderLog.event("offline preview OpenGL context ready after " + juce::String(waitedMs, 0) + " ms");
    }

    if (fps <= 0.0)
    {
        offlineRenderLog.event("render failed: invalid frame rate " + juce::String(fps, 2));
        result.errorMessage = "Invalid frame rate in Recording Settings.";
        return result;
    }

    // Decode via WavParser (AudioFormatManager-backed), but lock it to the file sample rate.
    WavParser wav;
    wav.setLooping(false);
    wav.setPaused(false);
    wav.setFollowProcessorSampleRate(false);

    std::unique_ptr<juce::InputStream> stream = inputAudioFile.createInputStream();
    if (!wav.parse(std::move(stream)))
    {
        offlineRenderLog.event("render failed: input audio decode failed");
        result.errorMessage = "Failed to decode the input audio file.";
        return result;
    }

    if (shouldCancel())
    {
        offlineRenderLog.event("render cancelled after input decode");
        result.cancelled = true;
        return result;
    }

    const double fileSampleRate = wav.getFileSampleRate();
    if (fileSampleRate <= 0.0)
    {
        offlineRenderLog.event("render failed: invalid decoded sample rate " + juce::String(fileSampleRate, 2));
        result.errorMessage = "Could not determine audio file sample rate.";
        return result;
    }

    wav.setTargetSampleRate(fileSampleRate);

    const int decodeChannels = juce::jmax(1, wav.getNumChannels());

    // Derive render mode from the audio file channel count.
    // >= 5: XYRGB, >= 3: XYZ, >= 2: XY, else: XY (mono -> duplicated).
    const auto derivedRenderMode = (decodeChannels >= 5)
        ? VisualiserRenderer::RenderMode::XYRGB
        : (decodeChannels >= 3)
            ? VisualiserRenderer::RenderMode::XYZ
            : VisualiserRenderer::RenderMode::XY;

    preview.setRenderMode(derivedRenderMode);

    const int samplesPerFrame = juce::jmax(1, (int) std::llround(fileSampleRate / fps));
    const juce::int64 totalSamples = (juce::int64) wav.totalSamples.load();
    const juce::int64 totalFrames = std::max<juce::int64>(1, (totalSamples + (juce::int64) samplesPerFrame - 1) / (juce::int64) samplesPerFrame);
    const double audioDurationSeconds = fileSampleRate > 0.0 ? (double) totalSamples / fileSampleRate : 0.0;

    offlineRenderLog.event(
        "input decoded: sampleRate=" + juce::String(fileSampleRate, 2)
        + ", channels=" + juce::String(decodeChannels)
        + ", samples=" + juce::String(totalSamples)
        + ", duration=" + durationSummary(audioDurationSeconds)
        + ", derivedMode=" + renderModeToString(derivedRenderMode)
        + ", samplesPerFrame=" + juce::String(samplesPerFrame)
        + ", totalFrames=" + juce::String(totalFrames));

    // Configure preview renderer to match Recording Settings.
    preview.setRenderSize(renderSize);
    preview.setFrameRate(fps);
    preview.prepareTask(fileSampleRate, samplesPerFrame);

    // Setup ffmpeg video encoder (raw RGBA frames piped to stdin)
    auto ffmpegFile = processor.getFFmpegFile();
    if (!ffmpegFile.existsAsFile())
    {
        offlineRenderLog.event("render failed: FFmpeg not available at " + ffmpegFile.getFullPathName());
        result.errorMessage = "FFmpeg not available.";
        return result;
    }

    FFmpegEncoderManager ffmpegEncoderManager(ffmpegFile);

    if (!ffmpegEncoderManager.supportsVideoCodec(codec)) {
        offlineRenderLog.event("render failed: selected video encoder unavailable");
        result.errorMessage = preserveAlpha
            ? "This FFmpeg installation does not include the ProRes 4444 encoder required for transparent video."
            : "This FFmpeg installation does not include an encoder for the selected video codec.";
        return result;
    }

    juce::TemporaryFile tempVideo(preserveAlpha ? ".mov" : "." + recordingSettings.getFileExtensionForCodec());
    const auto tempVideoFile = tempVideo.getFile();

    osci::WriteProcess ffmpegProcess;

    const juce::String encodeCmd = ffmpegEncoderManager.buildVideoEncodingCommand(
        codec,
        crf,
        renderSize.width,
        renderSize.height,
        fps,
        preset,
        tempVideoFile,
        preserveAlpha);

    offlineRenderLog.event(
        "starting FFmpeg video encoder: codec=" + videoCodecToString(codec)
        + ", crf=" + juce::String(crf)
        + ", preset=" + preset
        + ", tempVideo=" + tempVideoFile.getFileName());

    if (encodeCmd.isEmpty() || !ffmpegProcess.start(encodeCmd))
    {
        offlineRenderLog.event("render failed: FFmpeg video encoder did not start; command=" + encodeCmd);
        result.errorMessage = "Failed to start FFmpeg video encoder.";
        return result;
    }

    offlineRenderLog.event("FFmpeg video encoder started");

    // Prepare audio buffers.
    juce::AudioBuffer<float> decodeBuffer;
    decodeBuffer.setSize(decodeChannels, samplesPerFrame, false, true, true);

    juce::AudioBuffer<float> renderBuffer;
    renderBuffer.setSize(6, samplesPerFrame, false, true, true);

    // Fill constant channels once per frame.
    auto fillConstantChannels = [&]() {
        // Defaults used when the source file doesn't provide these channels.
        juce::FloatVectorOperations::fill(renderBuffer.getWritePointer(2), 1.0f, samplesPerFrame);
        juce::FloatVectorOperations::fill(renderBuffer.getWritePointer(3), 1.0f, samplesPerFrame);
        juce::FloatVectorOperations::fill(renderBuffer.getWritePointer(4), 1.0f, samplesPerFrame);
        juce::FloatVectorOperations::fill(renderBuffer.getWritePointer(5), 1.0f, samplesPerFrame);
    };

    juce::int64 framesWritten = 0;
    double lastHeartbeatMs = renderStartedMs;
    constexpr int heartbeatProgressStepPercent = 25;
    constexpr double heartbeatIntervalMs = 60000.0;
    int lastHeartbeatPercent = -heartbeatProgressStepPercent;
    int slowFrameWarningsLogged = 0;
    int noCaptureWarningsLogged = 0;
    bool slowFrameWarningsSuppressed = false;
    bool noCaptureWarningsSuppressed = false;

    offlineRenderLog.event(
        "render loop starting: totalFrames=" + juce::String(totalFrames)
        + ", expectedFrameBytes=" + juce::String((juce::int64) renderSize.width * (juce::int64) renderSize.height * 4)
        + ", heartbeat=60s/25%");

    for (juce::int64 frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
    {
        if (shouldCancel())
        {
            offlineRenderLog.event("render cancelled before frame " + juce::String(frameIndex + 1) + " of " + juce::String(totalFrames));
            result.cancelled = true;
            break;
        }

        decodeBuffer.clear();
        wav.processBlock(decodeBuffer);

        // Map decoded channels into the visualiser's expected 6-channel buffer.
        // 0: X, 1: Y, 2: Z/brightness, 3: R, 4: G, 5: B
        const float* ch0 = decodeBuffer.getReadPointer(0);
        const float* ch1 = (decodeChannels > 1) ? decodeBuffer.getReadPointer(1) : decodeBuffer.getReadPointer(0);

        juce::FloatVectorOperations::copy(renderBuffer.getWritePointer(0), ch0, samplesPerFrame);
        juce::FloatVectorOperations::copy(renderBuffer.getWritePointer(1), ch1, samplesPerFrame);

        fillConstantChannels();

        if (decodeChannels >= 3)
            juce::FloatVectorOperations::copy(renderBuffer.getWritePointer(2), decodeBuffer.getReadPointer(2), samplesPerFrame);

        if (decodeChannels >= 5)
        {
            // Use channels 2/3/4 as RGB, and set Z to 1.0 for XYRGB mode.
            juce::FloatVectorOperations::fill(renderBuffer.getWritePointer(2), 1.0f, samplesPerFrame);
            juce::FloatVectorOperations::copy(renderBuffer.getWritePointer(3), decodeBuffer.getReadPointer(2), samplesPerFrame);
            juce::FloatVectorOperations::copy(renderBuffer.getWritePointer(4), decodeBuffer.getReadPointer(3), samplesPerFrame);
            juce::FloatVectorOperations::copy(renderBuffer.getWritePointer(5), decodeBuffer.getReadPointer(4), samplesPerFrame);
        }

        // This blocks until the OpenGL thread has rendered.
        const double frameStartedMs = juce::Time::getMillisecondCounterHiRes();
        const int heartbeatPercent = (int) juce::jlimit(0.0, 100.0, std::floor(((double) frameIndex / (double) totalFrames) * 100.0));
        if (frameIndex == 0 || heartbeatPercent >= lastHeartbeatPercent + heartbeatProgressStepPercent || frameStartedMs - lastHeartbeatMs >= heartbeatIntervalMs)
        {
            offlineRenderLog.event(
                "rendering frame " + juce::String(frameIndex + 1) + " of " + juce::String(totalFrames)
                + " (" + juce::String(heartbeatPercent) + "%), elapsed="
                + durationSummary((frameStartedMs - renderStartedMs) / 1000.0)
                + ", capturedFrames=" + juce::String(capturedFrameCount.load()));
            lastHeartbeatMs = frameStartedMs;
            lastHeartbeatPercent = heartbeatPercent;
        }

        const int capturedBeforeRender = capturedFrameCount.load();
        preview.runTask(renderBuffer);
        const double frameRenderMs = juce::Time::getMillisecondCounterHiRes() - frameStartedMs;
        const int capturedAfterRender = capturedFrameCount.load();

        if (frameRenderMs >= 2500.0)
        {
            if (slowFrameWarningsLogged < 3)
            {
                offlineRenderLog.event(
                    "slow frame render: frame=" + juce::String(frameIndex + 1)
                    + ", renderMs=" + juce::String(frameRenderMs, 0)
                    + ", capturedBefore=" + juce::String(capturedBeforeRender)
                    + ", capturedAfter=" + juce::String(capturedAfterRender));
                slowFrameWarningsLogged++;
            }
            else if (!slowFrameWarningsSuppressed)
            {
                offlineRenderLog.event("additional slow frame render warnings suppressed for this export");
                slowFrameWarningsSuppressed = true;
            }
        }

        if (capturedAfterRender == capturedBeforeRender)
        {
            if (noCaptureWarningsLogged < 3)
            {
                offlineRenderLog.event(
                    "frame render returned without a new capture callback: frame=" + juce::String(frameIndex + 1)
                    + ", renderMs=" + juce::String(frameRenderMs, 0)
                    + ", capturedFrames=" + juce::String(capturedAfterRender));
                noCaptureWarningsLogged++;
            }
            else if (!noCaptureWarningsSuppressed)
            {
                offlineRenderLog.event("additional missing capture callback warnings suppressed for this export");
                noCaptureWarningsSuppressed = true;
            }
        }

        const auto tex = preview.getRenderTexture();
        const size_t expectedBytes = (size_t) (4 * tex.width * tex.height);

        {
            const juce::ScopedLock lock(frameLock);
            if (framePixels.empty())
            {
                offlineRenderLog.event(
                    "render failed: captured frame was empty at frame " + juce::String(frameIndex + 1)
                    + ", texture=" + juce::String(tex.width) + "x" + juce::String(tex.height)
                    + ", capturedFrames=" + juce::String(capturedAfterRender));
                result.errorMessage = "Failed to capture rendered frame.";
                break;
            }

            if (framePixels.size() != expectedBytes)
            {
                offlineRenderLog.event(
                    "render failed: captured frame size mismatch at frame " + juce::String(frameIndex + 1)
                    + ", got=" + juce::String((juce::int64) framePixels.size())
                    + ", expected=" + juce::String((juce::int64) expectedBytes)
                    + ", texture=" + juce::String(tex.width) + "x" + juce::String(tex.height));
                result.errorMessage = "Captured frame had unexpected size.";
                break;
            }

            if (shouldCancel())
            {
                offlineRenderLog.event("render cancelled before writing frame " + juce::String(frameIndex + 1));
                result.cancelled = true;
                break;
            }

            if (ffmpegProcess.write(framePixels.data(), expectedBytes, VideoEncodingConstants::frameWriteTimeoutMs) == 0)
            {
                offlineRenderLog.event(
                    "render failed: FFmpeg frame write failed at frame " + juce::String(frameIndex + 1)
                    + ", bytes=" + juce::String((juce::int64) expectedBytes)
                    + ", renderMs=" + juce::String(frameRenderMs, 0));
                result.errorMessage = "An error occurred while writing video frames to FFmpeg.";
                break;
            }

            framesWritten++;
        }

        if (result.cancelled)
        {
            break;
        }

        const double progress = (double) (frameIndex + 1) / (double) totalFrames;
        const int percent = (int) juce::jlimit(0.0, 100.0, std::floor(progress * 100.0));
        const int prevPercent = lastPostedProgressPercent.exchange(percent);
        if (percent != prevPercent)
        {
            setProgressAsync(progress);
        }
    }

    ffmpegProcess.close();
    offlineRenderLog.event(
        "render loop finished: framesWritten=" + juce::String(framesWritten)
        + " of " + juce::String(totalFrames)
        + ", capturedFrames=" + juce::String(capturedFrameCount.load())
        + ", elapsed=" + durationSummary((juce::Time::getMillisecondCounterHiRes() - renderStartedMs) / 1000.0)
        + ", cancelled=" + yesNo(result.cancelled)
        + ", error=" + yesNo(result.errorMessage.isNotEmpty()));

    if (result.cancelled)
    {
        offlineRenderLog.event("render returning cancelled");
        tempVideoFile.deleteFile();
        return result;
    }

    if (result.errorMessage.isNotEmpty())
    {
        offlineRenderLog.event("render returning error: " + result.errorMessage);
        tempVideoFile.deleteFile();
        return result;
    }

    if (!tempVideoFile.existsAsFile())
    {
        offlineRenderLog.event("render failed: FFmpeg did not produce temp video " + tempVideoFile.getFullPathName());
        result.errorMessage = "FFmpeg did not produce a video file.";
        return result;
    }

    // Write final output via a TemporaryFile tied to the chosen output path
    juce::TemporaryFile tempFinal(outputVideoFile);

    if (recordingSettings.recordingAudio())
    {
        juce::String muxError;
        const auto audioCodecArgs = recordingSettings.getAudioCodecArgs();
        offlineRenderLog.event("muxing audio into final video: audioCodecArgs=" + audioCodecArgs.joinIntoString(" "));
        if (!ffmpegEncoderManager.muxAudioAndVideo(tempVideoFile, inputAudioFile, tempFinal.getFile(), audioCodecArgs, muxError, &cancelRequested))
        {
            tempFinal.getFile().deleteFile();
            tempVideoFile.deleteFile();

            if (shouldCancel())
            {
                offlineRenderLog.event("mux cancelled");
                result.cancelled = true;
                return result;
            }

            result.errorMessage = muxError.isNotEmpty() ? muxError : "Failed to mux audio into video.";
            offlineRenderLog.event("mux failed: " + result.errorMessage.substring(0, 500));
            return result;
        }

        offlineRenderLog.event("mux complete");
    }
    else
    {
        // Video-only: just copy temp video into the tempFinal file.
        if (!tempVideoFile.copyFileTo(tempFinal.getFile()))
        {
            tempFinal.getFile().deleteFile();
            tempVideoFile.deleteFile();
            offlineRenderLog.event("render failed: could not copy temp video to final temporary file");
            result.errorMessage = "Failed to write output video file.";
            return result;
        }
    }

    // Atomically replace the destination.
    if (!tempFinal.overwriteTargetFileWithTemporary())
    {
        tempFinal.getFile().deleteFile();
        tempVideoFile.deleteFile();
        offlineRenderLog.event("render failed: could not finalise output file " + outputVideoFile.getFullPathName());
        result.errorMessage = "Failed to finalise output video file.";
        return result;
    }

    tempVideoFile.deleteFile();

    result.success = true;
    offlineRenderLog.event(
        "render succeeded: output=" + fileSummary(outputVideoFile)
        + ", totalElapsed=" + durationSummary((juce::Time::getMillisecondCounterHiRes() - renderStartedMs) / 1000.0));
    return result;
}

#endif // OSCI_PREMIUM
