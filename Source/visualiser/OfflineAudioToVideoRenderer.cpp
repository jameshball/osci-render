#include "OfflineAudioToVideoRenderer.h"

#if OSCI_PREMIUM

#include "../LookAndFeel.h"

juce::String OfflineAudioToVideoRendererComponent::toPercentString(double progress)
{
    const int pct = (int) juce::jlimit(0.0, 100.0, std::round(progress * 100.0));
    return juce::String(pct) + "%";
}

bool OfflineAudioToVideoRendererComponent::runFfmpegMux(const juce::File& ffmpegExe,
                                                       const juce::File& videoInput,
                                                       const juce::File& audioInput,
                                                       const juce::File& output,
                                                       const std::atomic<bool>& cancelRequested,
                                                       juce::String& outError)
{
    if (!ffmpegExe.existsAsFile())
    {
        outError = "FFmpeg executable not found.";
        return false;
    }

    if (!videoInput.existsAsFile())
    {
        outError = "Temporary video file was not created.";
        return false;
    }

    if (!audioInput.existsAsFile())
    {
        outError = "Input audio file not found.";
        return false;
    }

    juce::StringArray args;
    args.add(ffmpegExe.getFullPathName());
    args.addArray({"-hide_banner", "-loglevel", "error"});
    args.addArray({"-i", videoInput.getFullPathName()});
    args.addArray({"-i", audioInput.getFullPathName()});
    args.addArray({"-c:v", "copy"});
    args.addArray({"-c:a", "aac"});
    args.addArray({"-b:a", "384k"});
    args.addArray({"-shortest"});
    args.addArray({"-y", output.getFullPathName()});

    juce::ChildProcess process;
    if (!process.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        outError = "Failed to start FFmpeg for audio mux.";
        return false;
    }

    // Wait until completion, but allow cancellation.
    while (process.isRunning())
    {
        if (cancelRequested.load() || juce::Thread::currentThreadShouldExit())
        {
            process.kill();
            outError = "Cancelled.";
            return false;
        }

        process.waitForProcessToFinish(200);
    }

    const juce::String ffmpegOutput = process.readAllProcessOutput();

    // ChildProcess doesn't reliably expose an exit code across platforms; treat missing output file as failure.
    if (!output.existsAsFile())
    {
        outError = ffmpegOutput.isNotEmpty() ? ffmpegOutput : "FFmpeg mux failed.";
        return false;
    }

    if (ffmpegOutput.isNotEmpty())
    {
        // Sometimes ffmpeg prints warnings; keep it concise.
        outError = ffmpegOutput;
    }

    return true;
}

OfflineAudioToVideoRendererComponent::OfflinePreviewRenderer::OfflinePreviewRenderer(
    VisualiserParameters& parameters,
    osci::AudioBackgroundThreadManager& manager,
    juce::WaitableEvent& glReadyEventToSignal)
    : VisualiserRenderer(parameters, manager, 1024, 60.0, "Offline"),
      glReadyEvent(glReadyEventToSignal)
{
}

void OfflineAudioToVideoRendererComponent::OfflinePreviewRenderer::newOpenGLContextCreated()
{
    VisualiserRenderer::newOpenGLContextCreated();
    glReadyEvent.signal();
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
                                                                                                                                                    VisualiserRenderer::RenderMode initialRenderMode)
    : processor(processor),
      recordingSettings(recordingSettings),
      inputAudioFile(inputAudioFile),
      outputVideoFile(outputVideoFile),
            preview(visualiserParameters, threadManager, glReadyEvent),
            initialRenderMode(initialRenderMode)
{
    setOpaque(true);

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
    // This component is marked opaque, so it must fully paint its bounds.
    g.fillAll(Colours::dark);
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
        return;

    cancelRequested.store(false);
    progressValue = 0.0;
    lastPostedProgressPercent.store(0);
    setProgressAsync(0.0);

    worker = std::make_unique<WorkerThread>(*this);
    worker->startThread();
}

void OfflineAudioToVideoRendererComponent::cancel()
{
    cancelRequested.store(true);
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

        if (auto* window = safeThis->findParentComponentOfClass<juce::DialogWindow>())
            window->setName("Render Audio File to Video (" + toPercentString(safeThis->progressValue) + ")");
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

    auto shouldCancel = [&]() {
        return cancelRequested.load() || juce::Thread::currentThreadShouldExit();
    };

    if (shouldCancel())
    {
        result.cancelled = true;
        return result;
    }

    if (!inputAudioFile.existsAsFile())
    {
        result.errorMessage = "Input audio file not found.";
        return result;
    }

    // Wait for OpenGL to be ready so runTask() doesn't stall indefinitely.
    // Poll so cancellation can interrupt promptly.
    {
        bool ready = false;
        for (int i = 0; i < 80; ++i)
        {
            if (shouldCancel())
            {
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
            result.errorMessage = "Visualiser could not initialise OpenGL.";
            return result;
        }
    }

    const double fps = recordingSettings.getFrameRate();
    if (fps <= 0.0)
    {
        result.errorMessage = "Invalid frame rate in Recording Settings.";
        return result;
    }

    const int resolution = juce::jmax(16, recordingSettings.getResolution());

    // Decode via WavParser (AudioFormatManager-backed), but lock it to the file sample rate.
    WavParser wav { processor };
    wav.setLooping(false);
    wav.setPaused(false);
    wav.setFollowProcessorSampleRate(false);

    std::unique_ptr<juce::InputStream> stream = inputAudioFile.createInputStream();
    if (!wav.parse(std::move(stream)))
    {
        result.errorMessage = "Failed to decode the input audio file.";
        return result;
    }

    if (shouldCancel())
    {
        result.cancelled = true;
        return result;
    }

    const double fileSampleRate = wav.getFileSampleRate();
    if (fileSampleRate <= 0.0)
    {
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

    // Configure preview renderer to match Recording Settings.
    preview.setResolution(resolution);
    preview.setFrameRate(fps);
    preview.prepareTask(fileSampleRate, samplesPerFrame);

    const juce::int64 totalSamples = (juce::int64) wav.totalSamples.load();
    const juce::int64 totalFrames = std::max<juce::int64>(1, (totalSamples + (juce::int64) samplesPerFrame - 1) / (juce::int64) samplesPerFrame);

    // Setup ffmpeg video encoder (raw RGBA frames piped to stdin)
    auto ffmpegFile = processor.getFFmpegFile();
    if (!ffmpegFile.existsAsFile())
    {
        result.errorMessage = "FFmpeg not available.";
        return result;
    }

    FFmpegEncoderManager ffmpegEncoderManager(ffmpegFile);

    const auto codec = recordingSettings.getVideoCodec();
    const int crf = recordingSettings.getCRF();
    const auto preset = recordingSettings.getCompressionPreset();

    juce::TemporaryFile tempVideo("." + recordingSettings.getFileExtensionForCodec());
    const auto tempVideoFile = tempVideo.getFile();

    osci::WriteProcess ffmpegProcess;

    const juce::String encodeCmd = ffmpegEncoderManager.buildVideoEncodingCommand(
        codec,
        crf,
        resolution,
        resolution,
        fps,
        preset,
        tempVideoFile);

    if (!ffmpegProcess.start(encodeCmd))
    {
        result.errorMessage = "Failed to start FFmpeg video encoder.";
        return result;
    }

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

    for (juce::int64 frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
    {
        if (shouldCancel())
        {
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
        preview.runTask(renderBuffer);

        const auto tex = preview.getRenderTexture();
        const size_t expectedBytes = (size_t) (4 * tex.width * tex.height);

        {
            const juce::ScopedLock lock(frameLock);
            if (framePixels.empty())
            {
                result.errorMessage = "Failed to capture rendered frame.";
                break;
            }

            if (framePixels.size() != expectedBytes)
            {
                result.errorMessage = "Captured frame had unexpected size.";
                break;
            }

            if (shouldCancel())
            {
                result.cancelled = true;
                break;
            }

            // Use a short timeout so cancellation can be observed quickly.
            if (ffmpegProcess.write(framePixels.data(), expectedBytes, 250) == 0)
            {
                result.errorMessage = "An error occurred while writing video frames to FFmpeg.";
                break;
            }
        }

        if (result.cancelled)
            break;

        const double progress = (double) (frameIndex + 1) / (double) totalFrames;
        const int percent = (int) juce::jlimit(0.0, 100.0, std::floor(progress * 100.0));
        const int prevPercent = lastPostedProgressPercent.exchange(percent);
        if (percent != prevPercent)
            setProgressAsync(progress);
    }

    ffmpegProcess.close();

    if (result.cancelled)
    {
        tempVideoFile.deleteFile();
        return result;
    }

    if (result.errorMessage.isNotEmpty())
    {
        tempVideoFile.deleteFile();
        return result;
    }

    if (!tempVideoFile.existsAsFile())
    {
        result.errorMessage = "FFmpeg did not produce a video file.";
        return result;
    }

    // Write final output via a TemporaryFile tied to the chosen output path
    juce::TemporaryFile tempFinal(outputVideoFile);

    if (recordingSettings.recordingAudio())
    {
        juce::String muxError;
        if (!runFfmpegMux(ffmpegFile, tempVideoFile, inputAudioFile, tempFinal.getFile(), cancelRequested, muxError))
        {
            tempFinal.getFile().deleteFile();
            tempVideoFile.deleteFile();

            if (shouldCancel())
            {
                result.cancelled = true;
                return result;
            }

            result.errorMessage = muxError.isNotEmpty() ? muxError : "Failed to mux audio into video.";
            return result;
        }
    }
    else
    {
        // Video-only: just copy temp video into the tempFinal file.
        if (!tempVideoFile.copyFileTo(tempFinal.getFile()))
        {
            tempFinal.getFile().deleteFile();
            tempVideoFile.deleteFile();
            result.errorMessage = "Failed to write output video file.";
            return result;
        }
    }

    // Atomically replace the destination.
    if (!tempFinal.overwriteTargetFileWithTemporary())
    {
        tempFinal.getFile().deleteFile();
        tempVideoFile.deleteFile();
        result.errorMessage = "Failed to finalise output video file.";
        return result;
    }

    tempVideoFile.deleteFile();

    result.success = true;
    return result;
}

#endif // OSCI_PREMIUM
