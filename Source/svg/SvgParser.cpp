#include "SvgParser.h"
#include "../shape/Line.h"
#include "../shape/QuadraticBezierCurve.h"
#include "../shape/CubicBezierCurve.h"  // Add missing include

SvgParser::SvgParser(juce::String svgFile) {
	auto doc = juce::XmlDocument::parse(svgFile);
    if (doc != nullptr) {
        std::unique_ptr<juce::Drawable> svg = juce::Drawable::createFromSVG(*doc);
        juce::DrawableComposite* composite = dynamic_cast<juce::DrawableComposite*>(svg.get());
        if (composite != nullptr) {
            auto contentArea = composite->getContentArea();
            auto path = svg->getOutlineAsPath();
            // Apply translation to center the path
            path.applyTransform(juce::AffineTransform::translation(-contentArea.getX(), -contentArea.getY()));
            
            // Instead of separate scaling for width and height, just get the path as is
            pathToShapes(path, shapes, true); // Enable normalization
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

void SvgParser::pathToShapes(juce::Path& path, std::vector<std::unique_ptr<Shape>>& shapes, bool normalise) {
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
	
	// Normalize shapes if requested and if there are shapes to normalize
	if (normalise && !shapes.empty()) {
		// Find the bounding box of all shapes
		double minX = std::numeric_limits<double>::max();
		double minY = std::numeric_limits<double>::max();
		double maxX = std::numeric_limits<double>::lowest();
		double maxY = std::numeric_limits<double>::lowest();
		
		for (const auto& shape : shapes) {
			auto start = shape->nextVector(0);
			minX = std::min(minX, start.x);
			minY = std::min(minY, start.y);
			maxX = std::max(maxX, start.x);
			maxY = std::max(maxY, start.y);

			auto end = shape->nextVector(1);
			minX = std::min(minX, end.x);
			minY = std::min(minY, end.y);
			maxX = std::max(maxX, end.x);
			maxY = std::max(maxY, end.y);
		}
		
		// Calculate center of all shapes
		double centerX = (minX + maxX) / 2.0;
		double centerY = (minY + maxY) / 2.0;
		
		// Calculate uniform scale factor based on the maximum dimension
		double width = maxX - minX;
		double height = maxY - minY;
		double scaleFactor = 1.8 / std::max(width, height); // Scale to fit within -1 to 1
		
		// Apply translation and scaling to all shapes
		for (auto& shape : shapes) {
			// First center, then scale
			shape->translate(-centerX, -centerY, 0);
			shape->scale(scaleFactor, scaleFactor, 1);
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
