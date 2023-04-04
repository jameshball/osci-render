#include "FrameProducer.h"

FrameProducer::FrameProducer(FrameConsumer& fc, std::shared_ptr<FrameSource> fs) : frameConsumer(fc), frameSource(fs), juce::Thread("producer", 0) {}

FrameProducer::~FrameProducer() {
	frameSource->disable();
	stopThread(-1);
}

void FrameProducer::run() {
	while (!threadShouldExit() && frameSource->isActive()) {
		frameConsumer.addFrame(frameSource->nextFrame(), sourceFileIndex);
	}
}

void FrameProducer::setSource(std::shared_ptr<FrameSource> source, int fileIndex) {
	// TODO: should make this atomic
	frameSource = source;
	sourceFileIndex = fileIndex;
}
