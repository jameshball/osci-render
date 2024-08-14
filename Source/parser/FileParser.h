#pragma once

#include "FrameSource.h"
#include "../shape/Shape.h"
#include "../obj/WorldObject.h"
#include "../svg/SvgParser.h"
#include "../txt/TextParser.h"
#include "../gpla/BinaryLineArtParser.h"
#include "../lua/LuaParser.h"
#include "../img/ImageParser.h"

class OscirenderAudioProcessor;
class FileParser {
public:
	FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback = nullptr);

	void parse(juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font);
	std::vector<std::unique_ptr<Shape>> nextFrame();
	Point nextSample(lua_State*& L, LuaVariables& vars);
	void closeLua(lua_State*& L);
	bool isSample();
	bool isActive();
	void disable();
	void enable();

	std::shared_ptr<WorldObject> getObject();
	std::shared_ptr<SvgParser> getSvg();
	std::shared_ptr<TextParser> getText();
	std::shared_ptr<BinaryLineArtParser> getBinaryLineArt();
	std::shared_ptr<LuaParser> getLua();
	std::shared_ptr<ImageParser> getImg();

	bool isAnimatable = false;

private:
	OscirenderAudioProcessor& audioProcessor;

	bool active = true;
	bool sampleSource = false;

	juce::SpinLock lock;

	std::shared_ptr<WorldObject> object;
	std::shared_ptr<SvgParser> svg;
	std::shared_ptr<TextParser> text;
	std::shared_ptr<BinaryLineArtParser> gpla;
	std::shared_ptr<LuaParser> lua;
	std::shared_ptr<ImageParser> img;

	juce::String fallbackLuaScript = "return { 0.0, 0.0 }";

	std::function<void(int, juce::String, juce::String)> errorCallback;
};
