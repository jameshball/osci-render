#pragma once
#include <JuceHeader.h>
#include "../svg/SvgParser.h"

class LineArtParser {
public:
	LineArtParser(juce::String json);
	LineArtParser(char* data, int dataLength);
	~LineArtParser();

	void setFrame(int fNum);
	std::vector<std::unique_ptr<osci::Shape>> draw();

	static std::vector<std::vector<osci::Line>> parseJsonFrames(juce::String jsonStr);
	static std::vector<std::vector<osci::Line>> parseBinaryFrames(char* data, int dataLength);

	static std::vector<osci::Line> generateFrame(juce::Array < juce::var> objects, double focalLength);

	int numFrames = 0;
	int frameNumber = 0;
private:
	static std::vector<std::vector<osci::Line>> epicFail();
	static double makeDouble(int64_t data);
	static void makeChars(int64_t data, char* chars);
	static std::vector<std::vector<osci::Point>> reorderVertices(std::vector<std::vector<osci::Point>> vertices);
	static std::vector<osci::Line> assembleFrame(std::vector<std::vector<std::vector<osci::Point>>> allVertices, std::vector<std::vector<double>> allMatrices, double focalLength);
	std::vector<std::vector<osci::Line>> frames;
	
};
