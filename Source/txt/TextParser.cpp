#include "TextParser.h"
#include "../shape/Line.h"
#include "../shape/QuadraticBezierCurve.h"


TextParser::TextParser(juce::String text, juce::Font font) {
	juce::Path textPath;
	juce::GlyphArrangement glyphs;
	glyphs.addFittedText(font, text, -2, -2, 4, 4, juce::Justification::centred, 2);
	glyphs.createPath(textPath);

	juce::Path::Iterator pathIterator(textPath);
	double x = 0;
	double y = 0;
	double startX = 0;
	double startY = 0;
	
	while (pathIterator.next()) {
		auto type = pathIterator.elementType;
		
		switch (type) {
		case juce::Path::Iterator::PathElementType::startNewSubPath:
			x = pathIterator.x1;
			y = pathIterator.y1;
			startX = x;
			startY = y;
			break;
		case juce::Path::Iterator::PathElementType::lineTo:
			shapes.push_back(std::make_unique<Line>(x, -y, pathIterator.x1, -pathIterator.y1));
			x = pathIterator.x1;
			y = pathIterator.y1;
			break;
		case juce::Path::Iterator::PathElementType::quadraticTo:
			shapes.push_back(std::make_unique<QuadraticBezierCurve>(x, -y, pathIterator.x1, -pathIterator.y1, pathIterator.x2, -pathIterator.y2));
			x = pathIterator.x2;
			y = pathIterator.y2;
			break;
		case juce::Path::Iterator::PathElementType::cubicTo:
			shapes.push_back(std::make_unique<CubicBezierCurve>(x, -y, pathIterator.x1, -pathIterator.y1, pathIterator.x2, -pathIterator.y2, pathIterator.x3, -pathIterator.y3));
			x = pathIterator.x3;
			y = pathIterator.y3;
			break;
		case juce::Path::Iterator::PathElementType::closePath:
			shapes.push_back(std::make_unique<Line>(x, -y, startX, -startY));
			x = startX;
			y = startY;
			break;
		}
	}
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
