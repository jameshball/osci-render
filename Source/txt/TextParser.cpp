#include "TextParser.h"
#include "../svg/SvgParser.h"
#include "../PluginProcessor.h"


TextParser::TextParser(OscirenderAudioProcessor &p, juce::String text, juce::Font font) : audioProcessor(p), text(text) {
    parse(text, font);
}

TextParser::~TextParser() {
}

void TextParser::parse(juce::String text, juce::Font font) {
    lastFont = font;
    juce::Path textPath;
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(font, text, -2, -2, 4, 4, juce::Justification::centred, 2);
    glyphs.createPath(textPath);

    shapes = std::vector<std::unique_ptr<Shape>>();
    SvgParser::pathToShapes(textPath, shapes);
}

std::vector<std::unique_ptr<Shape>> TextParser::draw() {
    // reparse text if font changes
    if (audioProcessor.font != lastFont) {
        parse(text, audioProcessor.font);
    }
    
	// clone with deep copy
	std::vector<std::unique_ptr<Shape>> tempShapes;
	
	for (auto& shape : shapes) {
		tempShapes.push_back(shape->clone());
	}
	return tempShapes;
}
