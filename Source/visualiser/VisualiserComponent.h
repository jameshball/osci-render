#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cstdint>

#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "../audio/AudioRecorder.h"
#include <osci_gui/osci_gui.h>
#include "../components/timeline/TimelineComponent.h"
#include "../components/timeline/TimelineController.h"
#include "../video/FFmpegEncoderManager.h"
#include <osci_file_import/osci_file_import.h>
#include "RecordingSettings.h"
#include "VisualiserSettings.h"
#include <osci_gui/visualiser/osci_VisualiserRenderer.h>
#include <osci_texture_interop/osci_texture_interop.h>

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

class CommonPluginEditor;
class VisualiserWindow;
class VisualiserComponent : public VisualiserRenderer, public AudioPlayerListener, public juce::AudioProcessorParameter::Listener, private juce::Timer {
public:
    VisualiserComponent(
        CommonAudioProcessor& processor,
        CommonPluginEditor& editor,
        juce::File ffmpegFile,
        VisualiserSettings& settings,
        RecordingSettings& recordingSettings,
        VisualiserComponent* parent = nullptr,
        bool visualiserOnly = false);
    ~VisualiserComponent() override;

    std::function<void()> openSettings;
    std::function<void()> closeSettings;

    void enableFullScreen();
    void setFullScreen(bool fullScreen);
    void setFullScreenCallback(std::function<void(FullScreenMode)> callback);
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void setPaused(bool paused, bool affectAudio = true);
    bool isPaused() const;
    bool isTransparentBackgroundEnabled() const;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setRecording(bool recording);
    void childUpdated();
    void setPopoutAlwaysOnTop(bool alwaysOnTop);
    bool isPopoutAlwaysOnTop() const;
    void prepareOverlayFadeIn();
    void fadeInAfterOverlay();
    void cancelOverlayFadeIn();
    void updateRenderModeFromProcessor();
    void setTimelineController(std::shared_ptr<TimelineController> controller);
    void parserChanged() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    void refreshOpenGLSurfaceTransparency();
#endif

    enum ColourIds
    {
        buttonRowColourId          = 0x7205900,  /**< A colour to use to fill the button row. */
    };

private:
    friend class VisualiserWindow;

    class FadeCoverComponent : public juce::Component {
    public:
        FadeCoverComponent();
        void paint(juce::Graphics& g) override;
    };

    static constexpr int overlayFadeDurationMs = 225;

    void updatePausedState();
    void closePopout();
    bool isPrimaryVisualiser() const;
    void setOverlayFadeProgress(float progress);
    void refreshTextureOutputButton();
    void setTextureOutputEnabled(bool enabled);
    void requestTextureOutputService();
    void serviceTextureOutputFrame();
    void handleTextureOutputServiceResult(osci::texture::ServiceResult result);
    void timerCallback() override;

    std::atomic<bool> active = true;
    std::atomic<unsigned int> pendingParameterUpdates { 0 };
    bool pauseOnMouseUp = false;

    CommonAudioProcessor& audioProcessor;
    CommonPluginEditor& editor;
    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout;

    VisualiserSettings& settings;
    RecordingSettings& recordingSettings;

    bool visualiserOnly;

    // Timeline for controlling playback (audio, video, gif, gpla)
    // Controller is set by parent component based on file type
    TimelineComponent timeline;

    osci::SvgButton fullScreenButton{"fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white};
    osci::SvgButton popOutButton{"popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::red};
    osci::SvgButton settingsButton{"settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white};
    osci::SvgButton audioInputButton{"audioInput", BinaryData::microphone_svg, juce::Colours::white, juce::Colours::red};
    osci::SvgButton textureOutputButton{"textureOutput", BinaryData::spout_svg, juce::Colours::white, juce::Colours::red};
    osci::texture::OpenGLTexturePublisher textureOutputPublisher;

    int lastMouseX = 0;
    int lastMouseY = 0;
    int timerId = 0;
    int renderModeTimerId = 0;
    bool hideButtonRow = false;
    bool fullScreen = false;
    std::function<void(FullScreenMode)> fullScreenCallback;
    osci::ToggleAnimationController overlayFadeController { this };
    FadeCoverComponent overlayFadeCover;

    juce::File ffmpegFile;
    bool recordingAudio = true;

#if OSCI_PREMIUM
    bool recordingVideo = true;
    bool recordingTransparency = false;
    bool downloading = false;

    long numFrames = 0;
    VisualiserRenderSize recordingRenderSize;
    std::vector<unsigned char> framePixels;
    osci::WriteProcess ffmpegProcess;
    std::unique_ptr<juce::TemporaryFile> tempVideoFile;
    FFmpegEncoderManager ffmpegEncoderManager;
#endif

    StopwatchComponent stopwatch;
    osci::SvgButton record{"Record", BinaryData::record_svg, juce::Colours::red, juce::Colours::red.withAlpha(0.01f)};

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TemporaryFile> tempAudioFile;
    AudioRecorder audioRecorder;

    juce::Rectangle<int> buttonRow;

    void popoutWindow();
    void newOpenGLContextCreated() override;
    void openGLContextClosing() override;
    int prepareTask(double sampleRate, int samplesPerBlock) override;
    void stopTask() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE(VisualiserComponent)
};
