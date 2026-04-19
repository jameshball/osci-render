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

	// Offline / utility helpers
	// By default WavParser follows the processor sample rate (for realtime playback).
	// For offline rendering, you can disable following and set a fixed target sample rate
	// (often the file's own sample rate) to avoid resampling.
	void setFollowProcessorSampleRate(bool shouldFollow);
	void setTargetSampleRate(double sampleRate);
	double getFileSampleRate() const;
	int getNumChannels() const;

	void close();
	bool isInitialised();
    
    std::atomic<double> currentSample = 0;
    std::atomic<long> totalSamples;

private:
	void setSampleRate(double sampleRate);

	std::atomic<bool> initialised = false;
	std::unique_ptr<juce::AudioFormatReaderSource> afSource;
	std::atomic<bool> looping = true;
	std::unique_ptr<juce::ResamplingAudioSource> source = nullptr;
	juce::AudioBuffer<float> audioBuffer;
	std::atomic<bool> paused = false;
	double fileSampleRate = 0.0;
	double currentSampleRate = 0.0;
	std::atomic<bool> followProcessorSampleRate { true };
	std::atomic<double> targetSampleRate { 0.0 };
	int numChannels = 0;
    CommonAudioProcessor& audioProcessor;
};
