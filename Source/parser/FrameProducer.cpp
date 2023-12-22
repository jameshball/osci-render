#include "FrameProducer.h"

FrameProducer::FrameProducer(FrameConsumer& fc, std::shared_ptr<FileParser> fs) : frameConsumer(fc), frameSource(fs), juce::Thread("producer", 0) {}

FrameProducer::~FrameProducer() {}

void FrameProducer::run() {
	while (!threadShouldExit()) {
		auto frame = frameSource->nextFrame();
		frameConsumer.addFrame(frame);
	}
}
