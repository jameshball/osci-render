#pragma once

#include <algorithm>
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "../concurrency/ConsumerManager.h"
#include "../audio/SampleRateManager.h"
#include "LabelledTextBox.h"
#include "SvgButton.h"
#include "VisualiserSettings.h"
#include "VisualiserOpenGLComponent.h"
#include "StopwatchComponent.h"

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

class VisualiserWindow;
class VisualiserComponent : public juce::Component, public juce::Timer, public juce::Thread, public juce::MouseListener, public juce::SettableTooltipClient {
public:
    VisualiserComponent(SampleRateManager& sampleRateManager, ConsumerManager& consumerManager, VisualiserSettings& settings, VisualiserComponent* parent = nullptr, bool useOldVisualiser = false, bool visualiserOnly = false);
    ~VisualiserComponent() override;

    std::function<void()> openSettings;
    std::function<void()> closeSettings;

    void enableFullScreen();
    void setFullScreenCallback(std::function<void(FullScreenMode)> callback);
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void setBuffer(std::vector<Point>& buffer);
    void setColours(juce::Colour backgroundColour, juce::Colour waveformColour);
	void paintXY(juce::Graphics&, juce::Rectangle<float> bounds);
    void paint(juce::Graphics&) override;
    void resized() override;
	void timerCallback() override;
	void run() override;
    void setPaused(bool paused);
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setFullScreen(bool fullScreen);
    void setVisualiserType(bool oldVisualiser);
    void toggleRecording();
    void haltRecording();
    void childUpdated();

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;
    
    std::atomic<bool> active = true;
    
    std::function<void()> recordingHalted;

private:
    // 60fps
    const double BUFFER_LENGTH_SECS = 1/60.0;
    const double DEFAULT_SAMPLE_RATE = 192000.0;

    std::atomic<int> timerId;
    std::atomic<int> lastMouseX;
    std::atomic<int> lastMouseY;
    
    std::atomic<bool> oldVisualiser;
    
    bool visualiserOnly;
    
	juce::CriticalSection lock;
    std::vector<Point> buffer;
    std::vector<juce::Line<float>> prevLines;
    juce::Colour backgroundColour, waveformColour;
    SampleRateManager& sampleRateManager;
    ConsumerManager& consumerManager;
    int sampleRate = DEFAULT_SAMPLE_RATE;
    LabelledTextBox roughness{"Roughness", 1, 8, 1};
    LabelledTextBox intensity{"Intensity", 0, 1, 0.01};
    
    SvgButton fullScreenButton{ "fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white };
    SvgButton popOutButton{ "popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::white };
    SvgButton settingsButton{ "settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white };
    
    std::vector<Point> tempBuffer;
    int precision = 4;
    
    juce::CriticalSection consumerLock;
    std::shared_ptr<BufferConsumer> consumer;

    std::function<void(FullScreenMode)> fullScreenCallback;

    VisualiserSettings& settings;
    
    VisualiserOpenGLComponent openGLVisualiser {settings, sampleRateManager};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File tempVideoFile;
    
    StopwatchComponent stopwatch;
    SvgButton record{"Record", BinaryData::record_svg, juce::Colours::red, juce::Colours::red.withAlpha(0.01f)};
    
    void resetBuffer();
    void popoutWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE(VisualiserComponent)
};

class VisualiserWindow : public juce::DocumentWindow {
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent) : parent(parent), wasPaused(!parent->active), juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::TitleBarButtons::allButtons) {}
    
    void closeButtonPressed() override {
        parent->setPaused(wasPaused);
        parent->child = nullptr;
        parent->childUpdated();
        parent->resized();
        parent->popout.reset();
    }

private:
    VisualiserComponent* parent;
    bool wasPaused;
};
