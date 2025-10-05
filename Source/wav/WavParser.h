#pragma once
#include <JuceHeader.h>

class CommonAudioProcessor;
class WavParser {
public:
    WavParser(CommonAudioProcessor& p);
	~WavParser();

	void processBlock(juce::AudioBuffer<float>& buffer);

	void setProgress(double progress);
	void setPaused(bool paused);
	bool isPaused();
	void setLooping(bool looping);
	bool isLooping();
	bool parse(std::unique_ptr<juce::InputStream> stream);
	void close();
	bool isInitialised();
    
    std::atomic<double> currentSample = 0;
    std::atomic<long> totalSamples;

private:
	void setSampleRate(double sampleRate);

	std::atomic<bool> initialised = false;
	juce::AudioFormatReaderSource* afSource = nullptr;
	std::atomic<bool> looping = true;
	std::unique_ptr<juce::ResamplingAudioSource> source = nullptr;
	juce::AudioBuffer<float> audioBuffer;
	std::atomic<bool> paused = false;
	int fileSampleRate;
	int currentSampleRate;
    CommonAudioProcessor& audioProcessor;
};
