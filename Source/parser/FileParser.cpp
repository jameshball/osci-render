#include "FileParser.h"
#include "../shape/Line.h"

FileParser::FileParser() {}

void FileParser::parse(juce::String extension, std::unique_ptr<juce::InputStream> stream) {
	if (extension == ".obj") {
		object = std::make_unique<WorldObject>(*stream);
		camera = std::make_unique<Camera>(1.0, 0, 0, -1.0);
	}
}

std::vector<std::unique_ptr<Shape>> FileParser::next() {
	if (object != nullptr && camera != nullptr) {
		return camera->draw(*object);
	}
	auto shapes = std::vector<std::unique_ptr<Shape>>();
	shapes.push_back(std::make_unique<Line>(0.0, 0.0, 1.0, 1.0));
	return shapes;
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
