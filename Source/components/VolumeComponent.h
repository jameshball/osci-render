#pragma once

#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"

class VolumeComponent : public juce::Component, public juce::Timer, public juce::Thread {
public:
	VolumeComponent(OscirenderAudioProcessor& p);
    ~VolumeComponent() override;

    void paint(juce::Graphics&) override;
	void timerCallback() override;
	void run() override;
	void resized() override;

private:
	OscirenderAudioProcessor& audioProcessor;
	std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(1 << 11);

	std::atomic<float> leftVolume = 0;
	std::atomic<float> rightVolume = 0;
	std::atomic<float> avgLeftVolume = 0;
	std::atomic<float> avgRightVolume = 0;

	juce::Slider volumeSlider;
	juce::Slider thresholdSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VolumeComponent)
};