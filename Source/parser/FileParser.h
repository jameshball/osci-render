#pragma once

#include <JuceHeader.h>
#include "FrameSource.h"
#include "../shape/Shape.h"

class FileParser : public FrameSource {
public:
	FileParser();

	void parse() override;
	std::vector<std::unique_ptr<Shape>> next() override;
	bool isActive() override;
	void disable() override;
	void enable() override;

private:
	bool active = true;
};