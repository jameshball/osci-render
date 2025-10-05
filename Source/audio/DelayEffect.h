#pragma once
#include <JuceHeader.h>

class DelayEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point vector, osci::Point externalInput, const std::vector<std::atomic<float>>& values, float sampleRate) override {
		double decay = values[0];
		double decayLength = values[1];
		int delayBufferLength = (int)(sampleRate * decayLength);
		if (head >= delayBuffer.size()){
			head = 0;
		}
		if (position >= delayBuffer.size()){
			position = 0;
		}
		if (samplesSinceLastDelay >= delayBufferLength) {
			samplesSinceLastDelay = 0;
			position = head - delayBufferLength;
			if (position < 0) {
				position += delayBuffer.size();
			}
		}

		osci::Point echo = delayBuffer[position];
		vector = osci::Point(
			vector.x + echo.x * decay,
			vector.y + echo.y * decay,
			vector.z + echo.z * decay
		);

		delayBuffer[head] = vector;

		head++;
		position++;
		samplesSinceLastDelay++;

		return vector;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<DelayEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Delay Decay", "Adds repetitions, delays, or echos to the audio. This slider controls the volume of the echo.", "delayDecay", VERSION_HINT, 0.4, 0.0, 1.0),
				new osci::EffectParameter("Delay Length", "Controls the time in seconds between echos.", "delayLength", VERSION_HINT, 0.5, 0.0, 1.0)
			}
		);
		eff->setName("Delay");
		eff->setIcon(BinaryData::delay_svg);
		return eff;
	}

private:
	const static int MAX_DELAY = 192000 * 10;
	std::vector<osci::Point> delayBuffer = std::vector<osci::Point>(MAX_DELAY);
	int head = 0;
	int position = 0;
	int samplesSinceLastDelay = 0;
};
