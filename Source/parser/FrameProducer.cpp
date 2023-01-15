#include "FrameProducer.h"

FrameProducer::FrameProducer(FrameConsumer& fc, FrameSource& fs) : frameConsumer(fc), frameSource(fs), juce::Thread("producer", 0) {}

FrameProducer::~FrameProducer() {
	frameSource.disable();
	stopThread(-1);
}

void FrameProducer::run() {
	while (frameSource.isActive() && !threadShouldExit()) {
		frameConsumer.addFrame(frameSource.next());
	}
}
