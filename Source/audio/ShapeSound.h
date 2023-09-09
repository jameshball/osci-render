#pragma once
#include <JuceHeader.h>
#include "../parser/FileParser.h"
#include "../parser/FrameProducer.h"
#include "../parser/FrameConsumer.h"

class ShapeSound : public juce::SynthesiserSound, public FrameConsumer {
public:
	ShapeSound(std::shared_ptr<FileParser> parser);

	bool appliesToNote(int note) override;
	bool appliesToChannel(int channel) override;
	void addFrame(std::vector<std::unique_ptr<Shape>>& frame) override;
	double updateFrame(std::vector<std::unique_ptr<Shape>>& frame);

	std::shared_ptr<FileParser> parser;

	using Ptr = juce::ReferenceCountedObjectPtr<ShapeSound>;

private:
	
	juce::AbstractFifo frameFifo{ 10 };
	std::vector<std::unique_ptr<Shape>> frameBuffer[10];
	std::unique_ptr<FrameProducer> producer;
	double frameLength = 0.0;
};