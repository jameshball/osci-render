#pragma once
#include <JuceHeader.h>

class BulgeEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate) override {
		double value = values[0];
		double translatedBulge = -value + 1;

		double r = std::hypot(input.x, input.y);
        if (r == 0) return input;
		double rn = std::pow(r, translatedBulge);
		double scale = rn / r;
		return osci::Point(scale * input.x, scale * input.y, input.z);
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::SimpleEffect>(
			std::make_shared<BulgeEffect>(),
			new osci::EffectParameter("Bulge", "Applies a bulge that makes the centre of the image larger, and squishes the edges of the image. This applies a distortion to the audio.", "bulge", VERSION_HINT, 0.5, 0.0, 1.0));
		eff->setIcon(BinaryData::bulge_svg);
		return eff;
	}
};
