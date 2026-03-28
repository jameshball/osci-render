#pragma once
#include <JuceHeader.h>

class SvgParser {
public:
	SvgParser(juce::String svgFile);
	~SvgParser();

	static void pathToShapes(juce::Path& path, std::vector<std::unique_ptr<osci::Shape>>& shapes, bool normalise = false);

	std::vector<std::unique_ptr<osci::Shape>> draw();
private:
	
	std::vector<std::unique_ptr<osci::Shape>> shapes;
};
