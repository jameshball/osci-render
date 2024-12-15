#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>

class OscirenderAudioProcessor;
class WavParser {
public:
	WavParser(OscirenderAudioProcessor& p, std::unique_ptr<juce::InputStream> stream);
	~WavParser();

	OsciPoint getSample();

private:
	void setSampleRate(double sampleRate);

	std::unique_ptr<juce::ResamplingAudioSource> source;
	juce::AudioBuffer<float> audioBuffer;
    int currentSample = 0;
	int fileSampleRate;
	int currentSampleRate;
	OscirenderAudioProcessor& audioProcessor;
};
