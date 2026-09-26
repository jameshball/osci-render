#pragma once
#include <JuceHeader.h>
#include "PointSource.h"

class FrameProducer;
class ShapeSound : public juce::SynthesiserSound, public FrameConsumer {
public:
	explicit ShapeSound(std::shared_ptr<PointSource> source);
	ShapeSound();
	~ShapeSound() override;

	bool appliesToNote(int note) override;
	bool appliesToChannel(int channel) override;
	void addFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force = true) override;
	void replaceQueueWith(std::vector<std::unique_ptr<osci::Shape>>& frame) override;
	bool updateFrame(std::vector<std::unique_ptr<osci::Shape>>& frame);
	double getFrameLength() const;

	// Returns true (and clears the flag) when replaceQueueWith() has
	// pushed a fresh frame that the audio thread should grab immediately.
	bool consumeFreshFrame();

	std::shared_ptr<PointSource> parser;

	using Ptr = juce::ReferenceCountedObjectPtr<ShapeSound>;

private:
	osci::BlockingQueue frames{10};
	std::unique_ptr<FrameProducer> producer;
	std::atomic<double> frameLength{0.0};
	std::atomic<bool> freshFrameAvailable{false};
};
