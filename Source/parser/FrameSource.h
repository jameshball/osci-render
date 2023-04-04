#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "../shape/Shape.h"

class FrameSource {
public:
	virtual void parse(juce::String extension, std::unique_ptr<juce::InputStream>) = 0;
	virtual std::vector<std::unique_ptr<Shape>> nextFrame() = 0;
	virtual bool isActive() = 0;
	virtual void disable() = 0;
	virtual void enable() = 0;
};