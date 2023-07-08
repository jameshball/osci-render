#pragma once

#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"

class VisualiserComponent : public juce::Component {
public:
    VisualiserComponent(int numChannels, OscirenderAudioProcessor& p);
    ~VisualiserComponent() override;

    void setBuffer(std::vector<float>& buffer);
    void setColours(juce::Colour backgroundColour, juce::Colour waveformColour);
    void paintChannel(juce::Graphics&, juce::Rectangle<float> bounds, int channel);
	void paintXY(juce::Graphics&, juce::Rectangle<float> bounds);
    void paint(juce::Graphics&) override;

private:
	juce::SpinLock lock;
    std::vector<float> buffer;
    int numChannels = 2;
    juce::Colour backgroundColour, waveformColour;
	OscirenderAudioProcessor& audioProcessor;
	int precision = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
};

class VisualiserProcessor : public juce::AsyncUpdater, public juce::Thread {
public:
	VisualiserProcessor(std::shared_ptr<BufferConsumer> consumer, VisualiserComponent& visualiser) : juce::Thread("VisualiserProcessor"), consumer(consumer), visualiser(visualiser) {}
	~VisualiserProcessor() override {}

	void run() override {
		while (!threadShouldExit()) {
			auto buffer = consumer->startProcessing();

			visualiser.setBuffer(*buffer);
			consumer->finishedProcessing();
			triggerAsyncUpdate();
		}
	}

	void handleAsyncUpdate() override {
		visualiser.repaint();
	}

private:
	std::shared_ptr<BufferConsumer> consumer;
	VisualiserComponent& visualiser;
};