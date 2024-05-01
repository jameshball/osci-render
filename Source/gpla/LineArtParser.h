#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"

class LineArtParser {
public:
	LineArtParser(juce::String fileContents, bool b64 = false);
	~LineArtParser();

	void setFrame(int fNum);
	std::vector<std::unique_ptr<Shape>> draw();

	static std::vector<Line> generateFrame(std::vector< std::vector<std::vector<Point>>> inputVertices, std::vector<std::vector<double>> inputMatrices, double focalLength);
	static std::vector<Line> generateJsonFrame(juce::Array<juce::var> objects, double focalLength);
private:
	void parseJsonFrames(juce::String jsonStr);
	void parseBinaryFrames(juce::String base64);
	int frameNumber = 0;
	std::vector<std::vector<Line>> frames;
	int numFrames = 0;
};