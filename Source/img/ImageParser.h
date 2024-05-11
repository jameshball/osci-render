#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"

class OscirenderAudioProcessor;
class ImageParser {
public:
	ImageParser(OscirenderAudioProcessor& p, juce::String fileName, juce::MemoryBlock image);
	~ImageParser();

	void setFrame(int index);
	Point getSample();

private:
	void findNearestNeighbour(int searchRadius, float thresholdPow, int stride, bool invert);
	void resetPosition();

	OscirenderAudioProcessor& audioProcessor;
	juce::Random rng;
	int frameIndex = 0;
	std::vector<std::vector<uint8_t>> frames;
	std::vector<bool> visited;
	int currentX, currentY;
	int width, height;
	int count = 0;
};