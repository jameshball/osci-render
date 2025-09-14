#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class WobbleEffect : public osci::EffectApplication {
public:
	WobbleEffect(OscirenderAudioProcessor &p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate) override {
        double wobblePhase = values[1] * std::numbers::pi;
        double theta = nextPhase(audioProcessor.frequency, sampleRate) + wobblePhase;
        double delta = 0.5 * values[0] * std::sin(theta);

        return input + delta;
    }

	std::shared_ptr<osci::Effect> build() const override {
        auto wobble = std::make_shared<osci::SimpleEffect>(
            std::make_shared<WobbleEffect>(audioProcessor),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Wobble Amount", "Adds a sine wave of the prominent frequency in the audio currently playing. The sine wave's frequency is slightly offset to create a subtle 'wobble' in the image. Increasing the slider increases the strength of the wobble.", "wobble", VERSION_HINT, 0.3, 0.0, 1.0),
                new osci::EffectParameter("Wobble Phase", "Controls the phase of the wobble.", "wobblePhase", VERSION_HINT, 0.0, -1.0, 1.0, 0.0001f, osci::LfoType::Sawtooth, 1.0f),
            });
        wobble->setName("Wobble");
        wobble->setIcon(BinaryData::wobble_svg);
		return wobble;
	}

private:
    OscirenderAudioProcessor& audioProcessor;
	double smoothedFrequency = 0;
};
