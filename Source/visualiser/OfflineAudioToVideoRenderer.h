#pragma once

#include <JuceHeader.h>

#include "VisualiserRenderer.h"
#include "RecordingSettings.h"

#if OSCI_PREMIUM

#include "../CommonPluginProcessor.h"
#include "../video/FFmpegEncoderManager.h"
#include "../wav/WavParser.h"

class OfflineAudioToVideoRendererComponent;

class OfflineAudioToVideoRendererComponent : public juce::Component
{
public:
    struct Result
    {
        bool success = false;
        bool cancelled = false;
        juce::String errorMessage;
    };

    using FinishedCallback = std::function<void(Result)>;

    OfflineAudioToVideoRendererComponent(CommonAudioProcessor& processor,
                                        VisualiserParameters& visualiserParameters,
                                        osci::AudioBackgroundThreadManager& threadManager,
                                        RecordingSettings& recordingSettings,
                                        const juce::File& inputAudioFile,
                                        const juce::File& outputVideoFile,
                                        VisualiserRenderer::RenderMode initialRenderMode);

    ~OfflineAudioToVideoRendererComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void start();
    void cancel();

    void setOnFinished(FinishedCallback cb) { onFinished = std::move(cb); }

private:
    static juce::String toPercentString(double progress);

    static bool runFfmpegMux(const juce::File& ffmpegExe,
                             const juce::File& videoInput,
                             const juce::File& audioInput,
                             const juce::File& output,
                             const std::atomic<bool>& cancelRequested,
                             juce::String& outError);

    class OfflinePreviewRenderer : public VisualiserRenderer
    {
    public:
        OfflinePreviewRenderer(VisualiserParameters& parameters,
                               osci::AudioBackgroundThreadManager& manager,
                               juce::WaitableEvent& glReadyEventToSignal);

        void newOpenGLContextCreated() override;

        void setPostRenderCallback(std::function<void()> cb) { postRenderCallback = std::move(cb); }
        void setPreRenderCallback(std::function<void()> cb) { preRenderCallback = std::move(cb); }

    private:
        juce::WaitableEvent& glReadyEvent;
    };

    class WorkerThread : public juce::Thread
    {
    public:
        WorkerThread(OfflineAudioToVideoRendererComponent& owner);
        void run() override;

    private:
        OfflineAudioToVideoRendererComponent& owner;
    };

    Result renderToFile();

    void setProgressAsync(double newProgress);
    void finishAsync(Result r);

    CommonAudioProcessor& processor;
    RecordingSettings& recordingSettings;

    const juce::File inputAudioFile;
    const juce::File outputVideoFile;

    juce::WaitableEvent glReadyEvent;
    OfflinePreviewRenderer preview;

    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton { "Cancel" };

    const VisualiserRenderer::RenderMode initialRenderMode;

    std::atomic<bool> cancelRequested { false };
    std::unique_ptr<WorkerThread> worker;

    juce::CriticalSection frameLock;
    std::vector<unsigned char> framePixels;

    std::atomic<int> lastPostedProgressPercent { -1 };

    FinishedCallback onFinished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineAudioToVideoRendererComponent)
};

#endif // OSCI_PREMIUM
