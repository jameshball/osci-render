#pragma once

#include <JuceHeader.h>

#include <algorithm>

#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "../audio/AudioRecorder.h"
#include "../components/DownloaderComponent.h"
#include "../components/StopwatchComponent.h"
#include "../components/SvgButton.h"
#include "../components/timeline/TimelineComponent.h"
#include "../components/timeline/TimelineController.h"
#include "../video/FFmpegEncoderManager.h"
#include "../audio/wav/WavParser.h"
#include "RecordingSettings.h"
#include "VisualiserSettings.h"
#include "VisualiserRenderer.h"
#include "TransparentWindow.h"

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

// Semi-transparent floating toolbar for the popout window.
// Covers the full component area but only intercepts clicks in the toolbar zone.
class PopoutToolbar : public juce::Component {
public:
    std::function<void()> onClose;
    std::function<void()> onFullScreen;

    PopoutToolbar() : closeButton("close", BinaryData::close_svg, juce::Colours::white),
                      fullscreenButton("fullscreen", BinaryData::fullscreen_svg, juce::Colours::white) {
        addAndMakeVisible(closeButton);
        addAndMakeVisible(fullscreenButton);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        fullscreenButton.onClick = [this] { if (onFullScreen) onFullScreen(); };
        setInterceptsMouseClicks(true, true);
    }

    bool hitTest(int x, int y) override {
        if (y < toolbarHeight)
            return true;
        // Allow child buttons to get events even outside toolbar area
        for (auto* child : getChildren())
            if (child->getBounds().contains(x, y))
                return true;
        return false;
    }

    void paint(juce::Graphics& g) override {
        auto toolbarColour = juce::Colour((juce::uint8) 30, (juce::uint8) 30, (juce::uint8) 30, (juce::uint8) 255);

        // Toolbar bar at the top — full width, no rounding
        auto bar = getLocalBounds().removeFromTop(toolbarHeight).toFloat();
        g.setColour(toolbarColour);
        g.fillRect(bar);

        // Border outline around the entire component (inset to match window corner clip)
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.25f), 8.75f, 2.5f);
    }

    void resized() override {
        auto bar = getLocalBounds().removeFromTop(toolbarHeight);
        closeButton.setBounds(bar.removeFromLeft(toolbarHeight).reduced(4));
        fullscreenButton.setBounds(bar.removeFromRight(toolbarHeight).reduced(4));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragger.startDraggingComponent(getTopLevelComponent(),
            e.getEventRelativeTo(getTopLevelComponent()));
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        dragger.dragComponent(getTopLevelComponent(),
            e.getEventRelativeTo(getTopLevelComponent()), nullptr);
    }

private:
    static constexpr int toolbarHeight = 24;
    SvgButton closeButton;
    SvgButton fullscreenButton;
    juce::ComponentDragger dragger;
};

class CommonPluginEditor;
class VisualiserWindow;
class VisualiserComponent : public VisualiserRenderer, public juce::MouseListener, public AudioPlayerListener, public juce::AudioProcessorParameter::Listener {
public:
    VisualiserComponent(
        CommonAudioProcessor& processor,
        CommonPluginEditor& editor,
#if OSCI_PREMIUM
        SharedTextureManager& sharedTextureManager,
#endif
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
    bool keyPressed(const juce::KeyPress& key) override;
    void setRecording(bool recording);
    void childUpdated();
    void updateRenderModeFromProcessor();
    void setTimelineController(std::shared_ptr<TimelineController> controller);
    void parserChanged() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;

    void setPopoutToolbarVisible(bool visible) {
        if (popoutToolbar) {
            popoutToolbar->setVisible(visible);
            // Only pay the componentPainting cost when the toolbar is visible
            openGLContext.setComponentPaintingEnabled(visible);
        }
    }

    enum ColourIds
    {
        buttonRowColourId          = 0x7205900,  /**< A colour to use to fill the button row. */
    };

private:
    void updatePausedState();
    bool isPrimaryVisualiser() const;

    std::atomic<bool> active = true;

    CommonAudioProcessor& audioProcessor;
    CommonPluginEditor& editor;

    VisualiserSettings& settings;
    RecordingSettings& recordingSettings;

    bool visualiserOnly;
    
    // Timeline for controlling playback (audio, video, gif, gpla)
    // Controller is set by parent component based on file type
    TimelineComponent timeline;

    SvgButton fullScreenButton{"fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white};
    SvgButton popOutButton{"popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::red};
    SvgButton settingsButton{"settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white};
    SvgButton audioInputButton{"audioInput", BinaryData::microphone_svg, juce::Colours::white, juce::Colours::red};
    SvgButton sharedTextureButton{"sharedTexture", BinaryData::spout_svg, juce::Colours::white, juce::Colours::red};

#if OSCI_PREMIUM
    SharedTextureManager& sharedTextureManager;
    SharedTextureSender* sharedTextureSender = nullptr;
#endif

    int lastMouseX = 0;
    int lastMouseY = 0;
    int timerId = 0;
    int renderModeTimerId = 0;
    bool hideButtonRow = false;
    bool fullScreen = false;
    std::unique_ptr<PopoutToolbar> popoutToolbar;
    std::function<void(FullScreenMode)> fullScreenCallback;

    juce::File ffmpegFile;
    bool recordingAudio = true;

#if OSCI_PREMIUM
    bool recordingVideo = true;
    bool downloading = false;

    long numFrames = 0;
    std::vector<unsigned char> framePixels;
    osci::WriteProcess ffmpegProcess;
    std::unique_ptr<juce::TemporaryFile> tempVideoFile;
    FFmpegEncoderManager ffmpegEncoderManager;
#endif

    StopwatchComponent stopwatch;
    SvgButton record{"Record", BinaryData::record_svg, juce::Colours::red, juce::Colours::red.withAlpha(0.01f)};

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TemporaryFile> tempAudioFile;
    AudioRecorder audioRecorder;

    juce::Rectangle<int> buttonRow;

    void popoutWindow();
    void openGLContextClosing() override;
    int prepareTask(double sampleRate, int samplesPerBlock) override;
    void stopTask() override;

#if OSCI_PREMIUM
    void initialiseSharedTexture();
    void closeSharedTexture();
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE(VisualiserComponent)
};

class VisualiserWindow : public juce::DocumentWindow, private juce::Timer {
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent) : parent(parent), juce::DocumentWindow(name, juce::Colours::transparentBlack, 0) {
        setAlwaysOnTop(true);
        setTitleBarHeight(0);
        startTimerHz(4);
    }

    ~VisualiserWindow() override {
        stopTimer();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            closeButtonPressed();
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

    void closeButtonPressed() override {
        if (isFullScreen)
            toggleFullScreen();
        // local copy of parent so that we can safely delete the child
        VisualiserComponent* parent = this->parent;
        parent->setHasMirrorConsumer(false);
        parent->child = nullptr;
        parent->popout.reset();
        parent->childUpdated();
        parent->resized();
    }

    void toggleFullScreen() {
        isFullScreen = !isFullScreen;
        setAlwaysOnTop(!isFullScreen);
#if JUCE_WINDOWS
        if (isFullScreen) {
            windowedBounds = getBounds();
            auto& displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getDisplayForRect(getBounds());
            if (display != nullptr) {
                setFullScreen(true);
                setBounds(display->totalArea);
            }
        } else {
            setFullScreen(false);
            if (!windowedBounds.isEmpty())
                setBounds(windowedBounds);
        }
#else
        setFullScreen(isFullScreen);
#endif
        // Reapply transparent window config after fullscreen changes
        // (macOS may reset NSWindow properties during the transition).
        if (!isFullScreen) {
            configureNativeWindowTransparency(this);
            if (parent && parent->child)
                parent->child->resetGLSurfaceTransparency();
        }
    }

    bool getIsFullScreen() const { return isFullScreen; }

    void showToolbar(bool show) {
        if (toolbarVisible != show) {
            toolbarVisible = show;
            if (parent && parent->child)
                parent->child->setPopoutToolbarVisible(show);
        }
    }

    void moved() override { lastResizeTime = juce::Time::getMillisecondCounter(); }
    void resized() override {
        lastResizeTime = juce::Time::getMillisecondCounter();
        ResizableWindow::resized();
    }

private:
    void timerCallback() override {
        auto mouseScreenPos = juce::Desktop::getMousePosition();
        auto localPos = getLocalPoint(nullptr, mouseScreenPos);
        bool mouseInWindow = getLocalBounds().contains(localPos);
        // Keep toolbar visible during active resize/move (native drag doesn't report mouse state to JUCE)
        bool recentlyResized = (juce::Time::getMillisecondCounter() - lastResizeTime) < 500;
        showToolbar(mouseInWindow || recentlyResized);
    }

    VisualiserComponent* parent;
    bool isFullScreen = false;
    bool toolbarVisible = false;
    juce::uint32 lastResizeTime = 0;
    juce::Rectangle<int> windowedBounds;
};
