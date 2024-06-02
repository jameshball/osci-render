#pragma once

#include <algorithm>
#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"
#include "LabelledTextBox.h"
#include "SvgButton.h"

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

class VisualiserComponent : public juce::Component, public juce::Timer, public juce::Thread, public juce::MouseListener, public juce::SettableTooltipClient {
public:
    VisualiserComponent(int numChannels, OscirenderAudioProcessor& p);
    ~VisualiserComponent() override;

    void enableFullScreen();
    void setFullScreenCallback(std::function<void(FullScreenMode)> callback);
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void setBuffer(std::vector<float>& buffer);
    void setColours(juce::Colour backgroundColour, juce::Colour waveformColour);
    void paintChannel(juce::Graphics&, juce::Rectangle<float> bounds, int channel);
	void paintXY(juce::Graphics&, juce::Rectangle<float> bounds);
    void paint(juce::Graphics&) override;
    void resized() override;
	void timerCallback() override;
	void run() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    
    void setFullScreen(bool fullScreen);


private:
    const double BUFFER_LENGTH_SECS = 0.02;
    const double DEFAULT_SAMPLE_RATE = 192000.0;
    
    std::atomic<int> timerId;
    std::atomic<int> lastMouseX;
    std::atomic<int> lastMouseY;
    
	juce::CriticalSection lock;
    std::vector<float> buffer;
    std::vector<juce::Line<float>> prevLines;
    int numChannels = 2;
    juce::Colour backgroundColour, waveformColour;
	OscirenderAudioProcessor& audioProcessor;
    int sampleRate = DEFAULT_SAMPLE_RATE;
    LabelledTextBox roughness{"Roughness", 1, 8, 1};
    LabelledTextBox intensity{"Intensity", 0, 1, 0.01};
    
    SvgButton fullScreenButton{ "fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white };
    SvgButton settingsButton{ "settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white };
    
    std::vector<float> tempBuffer;
    int precision = 4;

    std::atomic<bool> active = true;
    
    std::shared_ptr<BufferConsumer> consumer;

    std::function<void(FullScreenMode)> fullScreenCallback;
    
    void resetBuffer();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
};
