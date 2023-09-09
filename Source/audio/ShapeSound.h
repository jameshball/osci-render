#pragma once
#include <JuceHeader.h>
#include "../parser/FileParser.h"
#include "../parser/FrameProducer.h"
#include "../parser/FrameConsumer.h"
#include "../concurrency/BlockingQueue.h"

class ShapeSound : public juce::SynthesiserSound, public FrameConsumer {
public:
	ShapeSound(std::shared_ptr<FileParser> parser);
	~ShapeSound() override;

	bool appliesToNote(int note) override;
	bool appliesToChannel(int channel) override;
	void addFrame(std::vector<std::unique_ptr<Shape>>& frame) override;
	double updateFrame(std::vector<std::unique_ptr<Shape>>& frame);

	std::shared_ptr<FileParser> parser;

	using Ptr = juce::ReferenceCountedObjectPtr<ShapeSound>;

private:
	
	BlockingQueue frames{10};
	std::unique_ptr<FrameProducer> producer;
	double frameLength = 0.0;
};