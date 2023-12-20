#pragma once

#include "FrameSource.h"
#include "../shape/Shape.h"
#include "../obj/WorldObject.h"
#include "../obj/Camera.h"
#include "../svg/SvgParser.h"
#include "../txt/TextParser.h"
#include "../lua/LuaParser.h"

class FileParser {
public:
	FileParser(std::function<void(int, juce::String, juce::String)> errorCallback = nullptr);

	void parse(juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream>, juce::Font);
	std::vector<std::unique_ptr<Shape>> nextFrame();
	Vector2 nextSample();
	bool isSample();
	bool isActive();
	void disable();
	void enable();

	std::shared_ptr<WorldObject> getObject();
	std::shared_ptr<Camera> getCamera();
	std::shared_ptr<SvgParser> getSvg();
	std::shared_ptr<TextParser> getText();
	std::shared_ptr<LuaParser> getLua();

private:
	bool active = true;
	bool sampleSource = false;

	juce::SpinLock lock;

	std::shared_ptr<WorldObject> object;
	std::shared_ptr<Camera> camera;
	std::shared_ptr<SvgParser> svg;
	std::shared_ptr<TextParser> text;
	std::shared_ptr<LuaParser> lua;

	juce::String fallbackLuaScript = "return { 0.0, 0.0 }";

	std::function<void(int, juce::String, juce::String)> errorCallback;
};
