#include "FileParser.h"
#include "../shape/Line.h"
#include "../shape/CircleArc.h"
#include <numbers>

FileParser::FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback) : errorCallback(errorCallback), audioProcessor(p) {}

void FileParser::parse(juce::String fileId, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font) {
	juce::SpinLock::ScopedLockType scope(lock);

	if (extension == ".lua" && lua != nullptr && lua->isFunctionValid()) {
		fallbackLuaScript = lua->getScript();
	}

	object = nullptr;
	svg = nullptr;
	text = nullptr;
	gpla = nullptr;
	lua = nullptr;
	img = nullptr;
	
	if (extension == ".obj") {
		object = std::make_shared<WorldObject>(stream->readEntireStreamAsString().toStdString());
	} else if (extension == ".svg") {
		svg = std::make_shared<SvgParser>(stream->readEntireStreamAsString());
	} else if (extension == ".txt") {
		text = std::make_shared<TextParser>(stream->readEntireStreamAsString(), font);
	} else if (extension == ".lua") {
		lua = std::make_shared<LuaParser>(fileId, stream->readEntireStreamAsString(), errorCallback, fallbackLuaScript);
	} else if (extension == ".gpla") {
		gpla = std::make_shared<LineArtParser>(stream->readEntireStreamAsString());
	} else if (extension == ".gif" || extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
		juce::MemoryBlock buffer{};
		int bytesRead = stream->readIntoMemoryBlock(buffer);
		img = std::make_shared<ImageParser>(audioProcessor, extension, buffer);
	}

	isAnimatable = gpla != nullptr || (img != nullptr && extension == ".gif");
	sampleSource = lua != nullptr || img != nullptr;
}

std::vector<std::unique_ptr<Shape>> FileParser::nextFrame() {
	juce::SpinLock::ScopedLockType scope(lock);

	if (object != nullptr) {
		return object->draw();
	} else if (svg != nullptr) {
		return svg->draw();
	} else if (text != nullptr) {
		return text->draw();
	} else if (gpla != nullptr) {
		return gpla->draw();
	}
	auto tempShapes = std::vector<std::unique_ptr<Shape>>();
	// return a square
	tempShapes.push_back(std::make_unique<Line>(Point(-0.5, -0.5, 0), Point(0.5, -0.5, 0)));
	tempShapes.push_back(std::make_unique<Line>(Point(0.5, -0.5, 0), Point(0.5, 0.5, 0)));
	tempShapes.push_back(std::make_unique<Line>(Point(0.5, 0.5, 0), Point(-0.5, 0.5, 0)));
	tempShapes.push_back(std::make_unique<Line>(Point(-0.5, 0.5, 0), Point(-0.5, -0.5, 0)));
	return tempShapes;
}

Point FileParser::nextSample(lua_State*& L, LuaVariables& vars) {
	juce::SpinLock::ScopedLockType scope(lock);

	if (lua != nullptr) {
		auto values = lua->run(L, vars);
		if (values.size() == 2) {
			return Point(values[0], values[1], 0);
		} else if (values.size() > 2) {
			return Point(values[0], values[1], values[2]);
		}
	} else if (img != nullptr) {
		return img->getSample();
	}

	return Point();
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

std::shared_ptr<LineArtParser> FileParser::getLineArt() {
	return gpla;
}

std::shared_ptr<LuaParser> FileParser::getLua() {
	return lua;
}

std::shared_ptr<ImageParser> FileParser::getImg() {
	return img;
}
