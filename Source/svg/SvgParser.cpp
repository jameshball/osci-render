#include "SvgParser.h"
#include "../shape/Line.h"
#include "../shape/QuadraticBezierCurve.h"


SvgParser::SvgParser(juce::String svgFile) {
	auto doc = juce::XmlDocument::parse(svgFile);
    if (doc != nullptr) {
        std::unique_ptr<juce::Drawable> svg = juce::Drawable::createFromSVG(*doc);
        juce::DrawableComposite* composite = dynamic_cast<juce::DrawableComposite*>(svg.get());
        if (composite != nullptr) {
            auto contentArea = composite->getContentArea();
            auto path = svg->getOutlineAsPath();
            // apply transform to path to get the content area in the bounds -1 to 1
            path.applyTransform(juce::AffineTransform::translation(-contentArea.getX(), -contentArea.getY()));
            path.applyTransform(juce::AffineTransform::scale(2 / contentArea.getWidth(), 2 / contentArea.getHeight()));
            path.applyTransform(juce::AffineTransform::translation(-1, -1));

            pathToShapes(path, shapes);
            Shape::removeOutOfBounds(shapes);
            return;
        }
    }
    
    juce::MessageManager::callAsync([this] {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::AlertIconType::WarningIcon, "Error", "The SVG could not be loaded.");
    });
    
    // draw an X to indicate an error.
    shapes.push_back(std::make_unique<Line>(-0.5, -0.5, 0.5, 0.5));
    shapes.push_back(std::make_unique<Line>(-0.5, 0.5, 0.5, -0.5));
}

SvgParser::~SvgParser() {}

void SvgParser::pathToShapes(juce::Path& path, std::vector<std::unique_ptr<Shape>>& shapes) {
	juce::Path::Iterator pathIterator(path);
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

std::vector<std::unique_ptr<Shape>> SvgParser::draw() {
	// clone with deep copy
	std::vector<std::unique_ptr<Shape>> tempShapes = std::vector<std::unique_ptr<Shape>>();
	
	for (auto& shape : shapes) {
		tempShapes.push_back(shape->clone());
	}
	return tempShapes;
}
