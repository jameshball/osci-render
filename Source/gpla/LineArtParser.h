#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"

class LineArtParser {
public:
	LineArtParser(juce::String json);
	LineArtParser(char* data, int dataLength);
	~LineArtParser();

	void setFrame(int fNum);
	std::vector<std::unique_ptr<Shape>> draw();

	static std::vector<Line> generateFrame(juce::Array < juce::var> objects, double focalLength);
private:
	void parseJsonFrames(juce::String jsonStr);
	void parseBinaryFrames(char* data, int dataLength);
	double makeDouble(int64_t data);
	void makeChars(int64_t data, char* chars);
	static std::vector<std::vector<OsciPoint>> reorderVertices(std::vector<std::vector<OsciPoint>> vertices);
	static std::vector<Line> assembleFrame(std::vector<std::vector<std::vector<OsciPoint>>> allVertices, std::vector<std::vector<double>> allMatrices, double focalLength);
	int frameNumber = 0;
	std::vector<std::vector<Line>> frames;
	int numFrames = 0;
};
