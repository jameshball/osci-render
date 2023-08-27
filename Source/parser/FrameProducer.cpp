#include "FrameProducer.h"

FrameProducer::FrameProducer(FrameConsumer& fc, std::shared_ptr<FileParser> fs) : frameConsumer(fc), frameSource(fs), juce::Thread("producer", 0) {}

FrameProducer::~FrameProducer() {
	frameSource->disable();
	stopThread(-1);
}

void FrameProducer::run() {
	while (!threadShouldExit()) {
		// this lock is needed so that frameSource isn't deleted whilst nextFrame() is being called
		juce::SpinLock::ScopedLockType scope(lock);
		frameConsumer.addFrame(frameSource->nextFrame(), sourceFileIndex);
	}
}

void FrameProducer::setSource(std::shared_ptr<FileParser> source, int fileIndex) {
	juce::SpinLock::ScopedLockType scope(lock);
	frameSource->disable();
	frameSource = source;
	sourceFileIndex = fileIndex;
}
