#pragma once
#include <JuceHeader.h>

class VectorCancellingEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate) override {
		double value = values[0];
		if (value < 0.001) {
			return input;
		}
		double frequency = 1.0 + 9.0 * value;
		if (index < lastIndex) {
			nextInvert = nextInvert - lastIndex + frequency;
		}
		lastIndex = index;
		if (index >= nextInvert) {
			nextInvert += frequency;
		} else {
			input.scale(-1, -1, 1);
		}
		return input;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<VectorCancellingEffect>(),
			new osci::EffectParameter("Vector Cancelling", "Inverts the audio and image every few samples to 'cancel out' the audio, making the audio quiet, and distorting the image.", "vectorCancelling", VERSION_HINT, 0.5, 0.0, 1.0));
		eff->setIcon(BinaryData::vectorcancelling_svg);
		return eff;
	}
private:
	int lastIndex = 0;
	double nextInvert = 0;
};
