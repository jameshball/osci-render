#pragma once

#include <JuceHeader.h>
#include "FrameSource.h"
#include "FrameConsumer.h"

class FrameProducer : public juce::Thread {
public:
	FrameProducer(FrameConsumer&, FrameSource&);
	~FrameProducer() override;

	void run() override;

private:
	FrameConsumer& frameConsumer;
	FrameSource& frameSource;
};