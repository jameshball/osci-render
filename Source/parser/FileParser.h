#pragma once

#include "FrameSource.h"
#include "../shape/Shape.h"
#include "../obj/WorldObject.h"
#include "../obj/Camera.h"

class FileParser : public FrameSource {
public:
	FileParser();

	void parse(juce::String extension, std::unique_ptr<juce::InputStream>) override;
	std::vector<std::unique_ptr<Shape>> next() override;
	bool isActive() override;
	void disable() override;
	void enable() override;

private:
	bool active = true;

	std::unique_ptr<WorldObject> object;
	std::unique_ptr<Camera> camera;
};