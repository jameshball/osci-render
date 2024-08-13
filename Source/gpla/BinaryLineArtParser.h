#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"

class BinaryLineArtParser {
public:
	BinaryLineArtParser(juce::MemoryBlock data, int size);
	~BinaryLineArtParser();

	void setFrame(int fNum);
	std::vector<std::unique_ptr<Shape>> draw();
	int numFrames = 0;

	static std::vector<Line> generateFrame(std::vector< std::vector<std::vector<Point>>> inputVertices, std::vector<std::vector<double>> inputMatrices, double focalLength);
private:
	void parseBinaryFrames(juce::MemoryBlock data, int size);
	void parsingFailed(int code);
	int frameNumber = 0;
	std::vector<std::vector<Line>> frames;
};