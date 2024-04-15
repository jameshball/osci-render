#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"

class LineArtParser {
public:
	LineArtParser(juce::String json);
	~LineArtParser();

	void setFrame(int fNum);
	std::vector<std::unique_ptr<Shape>> draw();
private:
	void parseJsonFrames(juce::String jsonStr);
	int frameNumber = 0;
	std::vector<std::vector<Line>> frames;
	int numFrames = 0;
	
};