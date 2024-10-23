#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"

class SvgParser {
public:
	SvgParser(juce::String svgFile);
	~SvgParser();

	static void pathToShapes(juce::Path& path, std::vector<std::unique_ptr<Shape>>& shapes);

	std::vector<std::unique_ptr<Shape>> draw();
private:
	
	std::vector<std::unique_ptr<Shape>> shapes;
};
