#pragma once

#include <JuceHeader.h>
#include "FileParser.h"
#include "FrameConsumer.h"

class FrameProducer : public juce::Thread {
public:
	FrameProducer(FrameConsumer&, std::shared_ptr<FileParser>);
	~FrameProducer() override;

	void run() override;
	void setSource(std::shared_ptr<FileParser>, int fileIndex);
private:
	juce::SpinLock lock;
	FrameConsumer& frameConsumer;
	std::shared_ptr<FileParser> frameSource;
	int sourceFileIndex = -1;
};