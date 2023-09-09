#pragma once

#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"

class VisualiserComponent : public juce::Component, public juce::Timer, public juce::Thread {
public:
    VisualiserComponent(int numChannels, OscirenderAudioProcessor& p);
    ~VisualiserComponent() override;

    void setBuffer(std::vector<float>& buffer);
    void setColours(juce::Colour backgroundColour, juce::Colour waveformColour);
    void paintChannel(juce::Graphics&, juce::Rectangle<float> bounds, int channel);
	void paintXY(juce::Graphics&, juce::Rectangle<float> bounds);
    void paint(juce::Graphics&) override;
	void timerCallback() override;
	void run() override;

private:
	juce::CriticalSection lock;
    std::vector<float> buffer;
    int numChannels = 2;
    juce::Colour backgroundColour, waveformColour;
	OscirenderAudioProcessor& audioProcessor;
    std::vector<float> tempBuffer = std::vector<float>(2 * 4096);
    int precision = 4;
    
    std::shared_ptr<BufferConsumer> consumer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
};
