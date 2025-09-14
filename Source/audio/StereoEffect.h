#pragma once
#include <JuceHeader.h>

class StereoEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		if (this->sampleRate != sampleRate) {
			this->sampleRate = sampleRate;
			initialiseBuffer(sampleRate);
		}
		double sampleOffset = values[0].load() / 10;
		sampleOffset = juce::jlimit(0.0, 1.0, sampleOffset);
		sampleOffset *= buffer.size();

		head++;
		if (head >= buffer.size()) {
			head = 0;
		}
		
		buffer[head] = input;

		int readHead = head - sampleOffset;
		if (readHead < 0) {
			readHead += buffer.size();
		}
		
		return osci::Point(input.x, buffer[readHead].y, input.z);
	}

	std::shared_ptr<osci::Effect> build() const override {
		return std::make_shared<osci::SimpleEffect>(
			std::make_shared<StereoEffect>(),
			new osci::EffectParameter(
				"Stereo",
				"Turns mono audio that is uninteresting to visualise into stereo audio that is interesting to visualise.",
				"stereo",
				VERSION_HINT, 0.0, 0.0, 1.0
			)
		);
	}

private:

	void initialiseBuffer(double sampleRate) {
		buffer.clear();
		buffer.resize(bufferLength * sampleRate);
		head = 0;
	}

	const double bufferLength = 0.1;
	double sampleRate = -1;
	std::vector<osci::Point> buffer;
	int head = 0;
};
