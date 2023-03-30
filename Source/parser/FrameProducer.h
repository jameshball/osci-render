#pragma once

#include <JuceHeader.h>
#include "FrameSource.h"
#include "FrameConsumer.h"

class FrameProducer : public juce::Thread {
public:
	FrameProducer(FrameConsumer&, std::shared_ptr<FrameSource>);
	~FrameProducer() override;

	void run() override;
	void setSource(std::shared_ptr<FrameSource>);
private:
	FrameConsumer& frameConsumer;
	std::shared_ptr<FrameSource> frameSource;
};