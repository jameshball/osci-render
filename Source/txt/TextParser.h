#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"

class TextParser {
public:
	TextParser(juce::String text, juce::Font font);
	~TextParser();

	std::vector<std::unique_ptr<Shape>> draw();
private:
	std::vector<std::unique_ptr<Shape>> shapes;
};