#include "FileParser.h"
#include "../shape/Line.h"
#include "../shape/Arc.h"
#include <numbers>

FileParser::FileParser() {}

void FileParser::parse(juce::String extension, std::unique_ptr<juce::InputStream> stream) {
	object = nullptr;
	camera = nullptr;
	svg = nullptr;
	
	if (extension == ".obj") {
		object = std::make_shared<WorldObject>(stream->readEntireStreamAsString().toStdString());
		camera = std::make_shared<Camera>(1.0, 0, 0, 0.0);
		camera->findZPos(*object);
	} else if (extension == ".svg") {
		svg = std::make_shared<SvgParser>(stream->readEntireStreamAsString());
	} else if (extension == ".txt") {
		text = std::make_shared<TextParser>(stream->readEntireStreamAsString(), juce::Font(1.0f));
	}
}

std::vector<std::unique_ptr<Shape>> FileParser::next() {
	auto tempObject = object;
	auto tempCamera = camera;
	auto tempSvg = svg;
	auto tempText = text;

	if (tempObject != nullptr && tempCamera != nullptr) {
		return tempCamera->draw(*tempObject);
	} else if (tempSvg != nullptr) {
		return tempSvg->draw();
	} else if (tempText != nullptr) {
		return tempText->draw();
	}
	auto tempShapes = std::vector<std::unique_ptr<Shape>>();
	tempShapes.push_back(std::make_unique<Arc>(0, 0, 0.5, 0.5, std::numbers::pi / 4.0, 2 * std::numbers::pi));
	return tempShapes;
}

bool FileParser::isActive() {
	return active;
}

void FileParser::disable() {
	active = false;
}

void FileParser::enable() {
	active = true;
}
