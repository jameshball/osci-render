#pragma once
#include <JuceHeader.h>
#include "../parser/FileParser.h"
#include "../parser/FrameProducer.h"
#include "../parser/FrameConsumer.h"

class OscirenderAudioProcessor;
class ShapeSound : public juce::SynthesiserSound, public FrameConsumer {
public:
	ShapeSound(OscirenderAudioProcessor &p, std::shared_ptr<FileParser> parser);
	ShapeSound();
	~ShapeSound() override;

	bool appliesToNote(int note) override;
	bool appliesToChannel(int channel) override;
	void addFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force = true) override;
	double updateFrame(std::vector<std::unique_ptr<osci::Shape>>& frame);

	std::shared_ptr<FileParser> parser;

	using Ptr = juce::ReferenceCountedObjectPtr<ShapeSound>;

private:
	
	osci::BlockingQueue frames{10};
	std::unique_ptr<FrameProducer> producer;
	double frameLength = 0.0;
};