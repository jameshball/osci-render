#pragma once
#include <JuceHeader.h>

class SmoothEffect : public osci::EffectApplication {
public:
	SmoothEffect() = default;
	explicit SmoothEffect(juce::String prefix, float defaultValue = 0.75f) : idPrefix(prefix), smoothingDefault(defaultValue) {}

	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double weight = juce::jmax(values[0].load(), 0.00001);
		weight *= 0.95;
		double strength = 10;
		weight = std::log(strength * weight + 1) / std::log(strength + 1);
		weight = std::pow(weight, 48000 / sampleRate);
		avg = weight * avg + (1 - weight) * input;
		
		return avg;
	}

	std::shared_ptr<osci::Effect> build() const override {
        auto id = idPrefix.isEmpty() ? juce::String("smoothing") : (idPrefix + "Smoothing");
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<SmoothEffect>(id),
	    	new osci::EffectParameter("Smoothing", "This works as a low-pass frequency filter that removes high frequencies, making the image look smoother, and audio sound less harsh.", id, VERSION_HINT, smoothingDefault, 0.0, 1.0)
		);
		eff->setIcon(BinaryData::smoothing_svg);
		return eff;
	}
private:
	osci::Point avg;
    juce::String idPrefix;
    float smoothingDefault = 0.5f;
};
