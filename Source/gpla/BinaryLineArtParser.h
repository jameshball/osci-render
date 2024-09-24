#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"

class BinaryLineArtParser {
public:
	BinaryLineArtParser(juce::MemoryBlock data, int size, bool json = false);
	~BinaryLineArtParser();

	void setFrame(int fNum);
	std::vector<std::unique_ptr<Shape>> draw();
	int numFrames = 0;

	static std::vector<Line> generateFrame(std::vector< std::vector<std::vector<Point>>> inputVertices, std::vector<std::vector<double>> inputMatrices, double focalLength);
	static std::vector<Line> generateJsonFrame(juce::Array<juce::var> objects, double focalLength);
private:
	void parseBinaryFrames(juce::MemoryBlock data, int size);
	void parseJsonFrames(juce::String jsonStr);
	void parsingFailed(int code);
	int frameNumber = 0;
	std::vector<std::vector<Line>> frames;
};