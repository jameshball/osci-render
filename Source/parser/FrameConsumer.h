#pragma once

#include <vector>
#include <memory>
#include <JuceHeader.h>

class FrameConsumer {
public:
	virtual void addFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force = true) = 0;
};
