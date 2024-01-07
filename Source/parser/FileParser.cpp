#include "FileParser.h"
#include "../shape/Line.h"
#include "../shape/CircleArc.h"
#include <numbers>

FileParser::FileParser(std::function<void(int, juce::String, juce::String)> errorCallback) : errorCallback(errorCallback) {}

void FileParser::parse(juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font) {
	juce::SpinLock::ScopedLockType scope(lock);

	if (extension == ".lua" && lua != nullptr && lua->isFunctionValid()) {
		fallbackLuaScript = lua->getScript();
	}

	object = nullptr;
	svg = nullptr;
	text = nullptr;
	lua = nullptr;
	
	if (extension == ".obj") {
		object = std::make_shared<WorldObject>(stream->readEntireStreamAsString().toStdString());
	} else if (extension == ".svg") {
		svg = std::make_shared<SvgParser>(stream->readEntireStreamAsString());
	} else if (extension == ".txt") {
		text = std::make_shared<TextParser>(stream->readEntireStreamAsString(), font);
	} else if (extension == ".lua") {
		lua = std::make_shared<LuaParser>(fileName, stream->readEntireStreamAsString(), errorCallback, fallbackLuaScript);
	}

	sampleSource = lua != nullptr;
}

std::vector<std::unique_ptr<Shape>> FileParser::nextFrame() {
	juce::SpinLock::ScopedLockType scope(lock);

	if (object != nullptr) {
		return object->draw();
	} else if (svg != nullptr) {
		return svg->draw();
	} else if (text != nullptr) {
		return text->draw();
	}
	auto tempShapes = std::vector<std::unique_ptr<Shape>>();
	tempShapes.push_back(std::make_unique<CircleArc>(0, 0, 0.5, 0.5, std::numbers::pi / 4.0, 2 * std::numbers::pi));
	return tempShapes;
}

Point FileParser::nextSample(lua_State*& L, const LuaVariables vars, long& step, double& phase) {
	juce::SpinLock::ScopedLockType scope(lock);

	if (lua != nullptr) {
		auto values = lua->run(L, vars, step, phase);
		if (values.size() < 2) {
            return Point();
        }
		return Point(values[0], values[1]);
	}
}

void FileParser::closeLua(lua_State*& L) {
	if (lua != nullptr) {
		lua->close(L);
    }
}

bool FileParser::isSample() {
	return sampleSource;
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

std::shared_ptr<WorldObject> FileParser::getObject() {
	return object;
}

std::shared_ptr<SvgParser> FileParser::getSvg() {
	return svg;
}

std::shared_ptr<TextParser> FileParser::getText() {
	return text;
}

std::shared_ptr<LuaParser> FileParser::getLua() {
	return lua;
}
