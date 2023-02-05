#pragma once
#include "../shape/Vector2.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "SvgState.h"
#include "../xml/pugixml.hpp"

class SvgParser {
public:
	SvgParser(juce::String svgFile);
	~SvgParser();

	std::vector<std::unique_ptr<Shape>> draw();
private:
	std::vector<std::string> preProcessPath(std::string path);
	juce::String simplifyLength(juce::String length);
	std::pair<double, double> getDimensions(pugi::xml_node&);
	std::vector<std::unique_ptr<Shape>> parsePath(juce::String);
	
	std::vector<std::unique_ptr<Shape>> shapes;
	SvgState state = SvgState();
};