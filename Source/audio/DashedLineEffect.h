#pragma once
#include <JuceHeader.h>

class DashedLineEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point vector, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		// dash length in seconds
		double dashLength = values[0] / 400;
		int dashLengthSamples = (int)(dashLength * sampleRate);
		dashLengthSamples = juce::jmin(dashLengthSamples, MAX_BUFFER);
		
		if (dashIndex >= dashLengthSamples) {
			dashIndex = 0;
			bufferIndex = 0;
		}

		buffer[bufferIndex] = vector;
		bufferIndex++;
		
		vector = buffer[dashIndex];
		
		if (index % 2 == 0) {
			dashIndex++;
		}
		
		return vector;
	}

private:
	const static int MAX_BUFFER = 192000;
	std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_BUFFER);
	int dashIndex = 0;
	int bufferIndex = 0;
};
