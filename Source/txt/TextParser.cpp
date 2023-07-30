#include "TextParser.h"
#include "../svg/SvgParser.h"


TextParser::TextParser(juce::String text, juce::Font font) {
	juce::Path textPath;
	juce::GlyphArrangement glyphs;
	glyphs.addFittedText(font, text, -2, -2, 4, 4, juce::Justification::centred, 2);
	glyphs.createPath(textPath);

	SvgParser::pathToShapes(textPath, shapes);
}

TextParser::~TextParser() {
}

std::vector<std::unique_ptr<Shape>> TextParser::draw() {
	// clone with deep copy
	std::vector<std::unique_ptr<Shape>> tempShapes;
	
	for (auto& shape : shapes) {
		tempShapes.push_back(shape->clone());
	}
	return tempShapes;
}
