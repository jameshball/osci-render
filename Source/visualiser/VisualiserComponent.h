#pragma once

#include <JuceHeader.h>

#include <algorithm>

#include "../LookAndFeel.h"
#include "../audio/AudioRecorder.h"
#include "../components/AudioPlayerComponent.h"
#include "../components/DownloaderComponent.h"
#include "../components/StopwatchComponent.h"
#include "../components/SvgButton.h"
#include "../img/qoixx.hpp"
#include "../video/FFmpegEncoderManager.h"
#include "../wav/WavParser.h"
#include "RecordingSettings.h"
#include "VisualiserSettings.h"
#include "VisualiserRenderer.h"

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

class CommonAudioProcessor;
class CommonPluginEditor;
class VisualiserWindow;
class VisualiserComponent : public VisualiserRenderer, public juce::MouseListener {
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
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setRecording(bool recording);
    void childUpdated();

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;

    std::atomic<bool> active = true;

private:
    CommonAudioProcessor& audioProcessor;
    CommonPluginEditor& editor;

    RecordingSettings& recordingSettings;

    bool visualiserOnly;
    AudioPlayerComponent audioPlayer{audioProcessor};

    SvgButton fullScreenButton{"fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white};
    SvgButton popOutButton{"popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::white};
    SvgButton settingsButton{"settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white};
    SvgButton audioInputButton{"audioInput", BinaryData::microphone_svg, juce::Colours::white, juce::Colours::red};

#if OSCI_PREMIUM
    SvgButton sharedTextureButton{"sharedTexture", BinaryData::spout_svg, juce::Colours::white, juce::Colours::red};
    SharedTextureManager& sharedTextureManager;
    SharedTextureSender* sharedTextureSender = nullptr;
#endif

    int lastMouseX = 0;
    int lastMouseY = 0;
    int timerId = 0;
    bool hideButtonRow = false;
    bool fullScreen = false;
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

class VisualiserWindow : public juce::DocumentWindow {
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent) : parent(parent), wasPaused(!parent->active), juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::TitleBarButtons::allButtons) {
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override {
        // local copy of parent so that we can safely delete the child
        VisualiserComponent* parent = this->parent;
        parent->setPaused(wasPaused);
        parent->child = nullptr;
        parent->popout.reset();
        parent->childUpdated();
        parent->resized();
    }

private:
    VisualiserComponent* parent;
    bool wasPaused;
};
