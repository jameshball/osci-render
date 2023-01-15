#include "FileParser.h"
#include "../shape/Line.h"

FileParser::FileParser() {}

void FileParser::parse() {
}

std::vector<std::unique_ptr<Shape>> FileParser::next() {
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
