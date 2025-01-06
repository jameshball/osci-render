#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>

class CommonAudioProcessor;
class WavParser {
public:
    WavParser(CommonAudioProcessor& p, std::unique_ptr<juce::InputStream> stream);
	~WavParser();

	OsciPoint getSample();

	void setProgress(double progress);
	void setPaused(bool paused);
	bool isPaused();
	void setLooping(bool looping);
	bool isLooping();

	std::function<void(double)> onProgress;

private:
	void setSampleRate(double sampleRate);

	juce::AudioFormatReaderSource* afSource;
	bool looping = true;
	std::unique_ptr<juce::ResamplingAudioSource> source;
	juce::AudioBuffer<float> audioBuffer;
	long totalSamples;
	long counter = 0;
    std::atomic<double> currentSample = 0;
	std::atomic<bool> paused = false;
	int fileSampleRate;
	int currentSampleRate;
    CommonAudioProcessor& audioProcessor;
};
