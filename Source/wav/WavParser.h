#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>

class CommonAudioProcessor;
class WavParser {
public:
    WavParser(CommonAudioProcessor& p);
	~WavParser();

	OsciPoint getSample();

	void setProgress(double progress);
	void setPaused(bool paused);
	bool isPaused();
	void setLooping(bool looping);
	bool isLooping();
	bool parse(std::unique_ptr<juce::InputStream> stream);
	void close();
	bool isInitialised();

	std::function<void(double)> onProgress;

private:
	void setSampleRate(double sampleRate);

	std::atomic<bool> initialised = false;
	juce::AudioFormatReaderSource* afSource = nullptr;
	std::atomic<bool> looping = true;
	std::unique_ptr<juce::ResamplingAudioSource> source = nullptr;
	juce::AudioBuffer<float> audioBuffer;
	std::atomic<long> totalSamples;
	std::atomic<long> counter = 0;
    std::atomic<double> currentSample = 0;
	std::atomic<bool> paused = false;
	int fileSampleRate;
	int currentSampleRate;
    CommonAudioProcessor& audioProcessor;
};
