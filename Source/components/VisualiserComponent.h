#pragma once

#include <algorithm>
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"
#include "LabelledTextBox.h"
#include "SvgButton.h"
#include "VisualiserSettings.h"

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

class VisualiserWindow;
class VisualiserComponent : public juce::Component, public juce::Timer, public juce::Thread, public juce::MouseListener, public juce::SettableTooltipClient {
public:
    VisualiserComponent(OscirenderAudioProcessor& p, VisualiserSettings& settings, VisualiserComponent* parent = nullptr, bool useOldVisualiser = false);
    ~VisualiserComponent() override;

    std::function<void()> openSettings;

    void childChanged();
    void enableFullScreen();
    void setFullScreenCallback(std::function<void(FullScreenMode)> callback);
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void setBuffer(std::vector<float>& buffer);
    void setColours(juce::Colour backgroundColour, juce::Colour waveformColour);
	void paintXY(juce::Graphics&, juce::Rectangle<float> bounds);
    void paint(juce::Graphics&) override;
    void resized() override;
	void timerCallback() override;
	void run() override;
    void setPaused(bool paused);
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setFullScreen(bool fullScreen);
    void setVisualiserType(bool oldVisualiser);

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;
    
    std::atomic<bool> active = true;

private:
    const double BUFFER_LENGTH_SECS = 0.02;
    const double DEFAULT_SAMPLE_RATE = 192000.0;

    
    std::atomic<int> timerId;
    std::atomic<int> lastMouseX;
    std::atomic<int> lastMouseY;
    
    bool oldVisualiser;
    
	juce::CriticalSection lock;
    std::vector<float> buffer;
    std::vector<juce::Line<float>> prevLines;
    juce::Colour backgroundColour, waveformColour;
	OscirenderAudioProcessor& audioProcessor;
    int sampleRate = DEFAULT_SAMPLE_RATE;
    LabelledTextBox roughness{"Roughness", 1, 8, 1};
    LabelledTextBox intensity{"Intensity", 0, 1, 0.01};
    
    SvgButton fullScreenButton{ "fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white };
    SvgButton popOutButton{ "popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::white };
    SvgButton settingsButton{ "settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white };
    
    std::vector<float> tempBuffer;
    int precision = 4;
    
    std::shared_ptr<BufferConsumer> consumer;

    std::function<void(FullScreenMode)> fullScreenCallback;

    VisualiserSettings& settings;
    
    juce::WebBrowserComponent::ResourceProvider provider = [this](const juce::String& path) {
        juce::String mimeType;
        if (path.endsWith("audio")) {
            mimeType = "application/octet-stream";
            juce::CriticalSection::ScopedLockType scope(lock);
            std::vector<std::byte> data(buffer.size() * sizeof(float));
            std::memcpy(data.data(), buffer.data(), data.size());
            juce::WebBrowserComponent::Resource resource = { data, mimeType };
            return resource;
        } else if (path.endsWith(".html")) {
            mimeType = "text/html";
        } else if (path.endsWith(".jpg")) {
            mimeType = "image/jpeg";
        } else if (path.endsWith(".js")) {
            mimeType = "text/javascript";
        }  else if (path.endsWith(".svg")) {
            mimeType = "image/svg+xml";
        }
        std::vector<std::byte> data;
        int size;
        const char* file = BinaryData::getNamedResource(path.substring(1).replaceCharacter('.', '_').toRawUTF8(), size);
        for (int i = 0; i < size; i++) {
            data.push_back((std::byte) file[i]);
        }
        juce::WebBrowserComponent::Resource resource = { data, mimeType };
        return resource;
    };

    std::unique_ptr<juce::WebBrowserComponent> browser = nullptr;
    
    void initialiseBrowser();
    void resetBuffer();
    void popoutWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
    juce::WeakReference<VisualiserComponent>::Master masterReference;
    friend class juce::WeakReference<VisualiserComponent>;
};

class VisualiserWindow : public juce::DocumentWindow {
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent) : parent(parent), wasPaused(!parent->active), juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::TitleBarButtons::allButtons) {}
    
    void closeButtonPressed() override {
        parent->setPaused(wasPaused);
        parent->child = nullptr;
        parent->childChanged();
        parent->resized();
        parent->popout.reset();
    }

private:
    VisualiserComponent* parent;
    bool wasPaused;
};
