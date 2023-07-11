#pragma once

#include <JuceHeader.h>
#include "FrameSource.h"
#include "FrameConsumer.h"

class FrameProducer : public juce::Thread {
public:
	FrameProducer(FrameConsumer&, std::shared_ptr<FrameSource>);
	~FrameProducer() override;

	void run() override;
	void setSource(std::shared_ptr<FrameSource>, int fileIndex);
private:
	juce::SpinLock lock;
	FrameConsumer& frameConsumer;
	std::shared_ptr<FrameSource> frameSource;
	int sourceFileIndex = -1;
};