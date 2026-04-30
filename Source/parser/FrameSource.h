#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>

class FrameSource {
public:
	virtual ~FrameSource() = default;
	virtual std::vector<std::unique_ptr<osci::Shape>> nextFrame() = 0;
	virtual osci::Point nextSample() = 0;
	virtual bool isSample() = 0;
	virtual bool isActive() = 0;
	virtual void disable() = 0;
	virtual void enable() = 0;

	// Returns true (and atomically clears the flag) if the source's
	// parameters have changed since the last call.  Used by FrameProducer
	// to decide whether to flush stale frames from the queue.
	virtual bool consumeDirty() { return false; }
};
