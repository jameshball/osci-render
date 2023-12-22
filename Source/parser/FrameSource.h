#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "../shape/Shape.h"

class FrameSource {
public:
	virtual std::vector<std::unique_ptr<Shape>> nextFrame() = 0;
	virtual Vector2 nextSample() = 0;
	virtual bool isSample() = 0;
	virtual bool isActive() = 0;
	virtual void disable() = 0;
	virtual void enable() = 0;
};