#include "FrameProducer.h"
#include "FileParser.h"

FrameProducer::FrameProducer(FrameConsumer& fc, std::shared_ptr<FileParser> fs) : juce::Thread("producer", 0), frameConsumer(fc), frameSource(fs) {}

FrameProducer::~FrameProducer() {}

void FrameProducer::run() {
	while (!threadShouldExit()) {
		// Note: there is a small TOCTOU gap between consumeDirty() and
		// nextFrame().  If a parameter changes after consumeDirty() returns
		// false but before nextFrame() runs, one frame at the new parameters
		// may be queued normally rather than via replaceQueueWith().  The
		// next iteration will catch the dirty flag and flush.  This is
		// imperceptible in practice.
		bool dirty = frameSource->consumeDirty();
		auto frame = frameSource->nextFrame();
		if (dirty) {
			frameConsumer.replaceQueueWith(frame);
		} else {
			frameConsumer.addFrame(frame);
		}
	}
}
