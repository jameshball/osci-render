#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>

class FrameSource {
public:
	virtual std::vector<std::unique_ptr<osci::Shape>> nextFrame() = 0;
	virtual osci::Point nextSample() = 0;
	virtual bool isSample() = 0;
	virtual bool isActive() = 0;
	virtual void disable() = 0;
	virtual void enable() = 0;
};
