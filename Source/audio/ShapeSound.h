#pragma once
#include <JuceHeader.h>
#include "../parser/FileParser.h"
#include "../parser/FrameProducer.h"
#include "../parser/FrameConsumer.h"
#include "../concurrency/BlockingQueue.h"

class ShapeSound : public juce::SynthesiserSound, public FrameConsumer {
public:
	ShapeSound(std::shared_ptr<FileParser> parser);
	ShapeSound();
	~ShapeSound() override;

	bool appliesToNote(int note) override;
	bool appliesToChannel(int channel) override;
	void addFrame(std::vector<std::unique_ptr<Shape>>& frame, bool force = true) override;
	double updateFrame(std::vector<std::unique_ptr<Shape>>& frame);
	double flushFrame(std::vector<std::unique_ptr<Shape>>& frame);

	std::shared_ptr<FileParser> parser;

	using Ptr = juce::ReferenceCountedObjectPtr<ShapeSound>;

private:
	
	BlockingQueue frames{10};
	std::unique_ptr<FrameProducer> producer;
	double frameLength = 0.0;
};