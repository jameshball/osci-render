#include "DashedLineEffect.h"

DashedLineEffect::DashedLineEffect() {}

DashedLineEffect::~DashedLineEffect() {}

osci::Point DashedLineEffect::apply(int index, osci::Point vector, const std::vector<std::atomic<double>>& values, double sampleRate) {
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
