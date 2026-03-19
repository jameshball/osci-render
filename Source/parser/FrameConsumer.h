#pragma once

#include <vector>
#include <memory>
#include <JuceHeader.h>

class FrameConsumer {
public:
	virtual ~FrameConsumer() = default;
	virtual void addFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force = true) = 0;

	// Flush all stale frames from the queue, then add the given frame and
	// signal that a fresh (urgent) frame is available for immediate
	// consumption.  Default implementation just calls addFrame().
	virtual void replaceQueueWith(std::vector<std::unique_ptr<osci::Shape>>& frame) { addFrame(frame); }
};
