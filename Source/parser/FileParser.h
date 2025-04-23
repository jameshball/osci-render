#pragma once

#include <JuceHeader.h>
#include "FrameSource.h"
#include "../obj/WorldObject.h"
#include "../svg/SvgParser.h"
#include "../txt/TextParser.h"
#include "../gpla/LineArtParser.h"
#include "../lua/LuaParser.h"
#include "../img/ImageParser.h"
#include "../wav/WavParser.h"

class OscirenderAudioProcessor;
class FileParser {
public:
	FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback = nullptr);

	void parse(juce::String fileId, juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font);
	std::vector<std::unique_ptr<osci::Shape>> nextFrame();
	osci::Point nextSample(lua_State*& L, LuaVariables& vars);

	bool isSample();
	bool isActive();
	void disable();
	void enable();
    
    int getNumFrames();
    int getCurrentFrame();
    void setFrame(int frame);
	
	std::shared_ptr<WorldObject> getObject();
	std::shared_ptr<SvgParser> getSvg();
	std::shared_ptr<TextParser> getText();
	std::shared_ptr<LineArtParser> getLineArt();
	std::shared_ptr<LuaParser> getLua();
	std::shared_ptr<ImageParser> getImg();
	std::shared_ptr<WavParser> getWav();

	bool isAnimatable = false;

private:
	OscirenderAudioProcessor& audioProcessor;

	bool active = true;
	bool sampleSource = false;
	juce::SpinLock lock;

	std::shared_ptr<WorldObject> object;
	std::shared_ptr<SvgParser> svg;
	std::shared_ptr<TextParser> text;
	std::shared_ptr<LineArtParser> gpla;
	std::shared_ptr<LuaParser> lua;
	std::shared_ptr<ImageParser> img;
	std::shared_ptr<WavParser> wav;

	juce::String fallbackLuaScript = "return { 0.0, 0.0 }";

	std::function<void(int, juce::String, juce::String)> errorCallback;
};
