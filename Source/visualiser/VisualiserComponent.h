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
#include "PopoutInteractionGeometry.h"
#include "PopoutPresentationState.h"
#include "TransparentWindow.h"
#include "VisualiserSettings.h"
#include <osci_gui/visualiser/osci_VisualiserRenderer.h>
#include <osci_texture_interop/osci_texture_interop.h>

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
class PopoutToolbar : public juce::Component {
public:
    PopoutToolbar();

    std::function<void()> onClose;
    std::function<void()> onFullScreen;
    std::function<void()> onToggleFrame;
    std::function<void()> onToggleAlwaysOnTop;

    void setState(bool visible, bool requestedVisible, bool alwaysOnTop, bool fullScreen);

    bool hitTest(int x, int y) override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    static constexpr int toolbarHeight = 24;

    osci::CloseButton closeButton;
    osci::SvgButton fullscreenButton{"fullscreen", BinaryData::fullscreen_svg, juce::Colours::white};
    osci::SvgButton frameButton{"popoutFrame", BinaryData::eye_svg, juce::Colours::white, juce::Colours::white,
                                nullptr, BinaryData::eyeoff_svg};
    osci::SvgButton alwaysOnTopButton{"alwaysOnTop", BinaryData::lock_svg, juce::Colours::white, juce::Colours::green};
    juce::ComponentDragger dragger;
    bool frameVisible = true;
    bool fullScreen = false;
};
#endif

class CommonPluginEditor;
class VisualiserWindow;
class VisualiserComponent : public VisualiserRenderer, public juce::MouseListener, public AudioPlayerListener, public juce::AudioProcessorParameter::Listener {
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
    void setPopoutPresentationOverlay(bool frameVisible, bool requestedFrameVisible, bool alwaysOnTop, bool fullScreen);
#endif
#if OSCI_PREMIUM && JUCE_MAC
    void refreshOpenGLSurfaceTransparency();
#endif

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;

    enum ColourIds
    {
        buttonRowColourId          = 0x7205900,  /**< A colour to use to fill the button row. */
    };

private:
    class FadeCoverComponent : public juce::Component {
    public:
        FadeCoverComponent();
        void paint(juce::Graphics& g) override;
    };

    static constexpr int overlayFadeDurationMs = 225;

    void updatePausedState();
    bool isPrimaryVisualiser() const;
    void setOverlayFadeProgress(float progress);
    void refreshTextureOutputButton();
    void setTextureOutputEnabled(bool enabled);
    void requestTextureOutputService();
    void serviceTextureOutputFrame();
    void handleTextureOutputServiceResult(osci::texture::ServiceResult result);

    std::atomic<bool> active = true;
    bool pauseOnMouseUp = false;

    CommonAudioProcessor& audioProcessor;
    CommonPluginEditor& editor;

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
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    std::unique_ptr<PopoutToolbar> popoutToolbar;
#endif
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

class VisualiserWindow : public juce::DocumentWindow
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    , private juce::Timer
#endif
{
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent, bool pinned);
    ~VisualiserWindow() override;

    bool keyPressed(const juce::KeyPress& key) override;
    void closeButtonPressed() override;
    void toggleFullScreen();

    bool getIsFullScreen() const { return fullScreenRequested; }
    bool isPinned() const { return pinned; }
    void setPinned(bool shouldBePinned);

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    void setRequestedFrameVisible(bool visible);
    bool isFrameRequestedVisible() const { return presentationState.requestedFrameVisible; }
    void refreshNativePresentation();
    void resized() override;
#endif

private:
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    void timerCallback() override;
    void updatePresentationState();
    void applyNativeInteraction(bool ignoresMouseEvents);
    void updateResizeBorderVisibility(bool visible);
#endif

    VisualiserComponent* parent;
    bool fullScreenRequested = false;
    bool fullScreenTransitionPending = false;
    bool pinned = true;
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    PopoutPresentationState presentationState;
    bool nativeIgnoresMouseEvents = false;
    bool presentationRefreshActive = false;
    bool windowCornersConfigured = false;
    bool windowCornersRounded = false;
    std::uint64_t presentationRefreshGeneration = 0;
#endif
};
