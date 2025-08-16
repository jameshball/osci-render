#pragma once
#include <JuceHeader.h>

class BulgeEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double value = values[0];
		double translatedBulge = -value + 1;

		double r = sqrt(pow(input.x, 2) + pow(input.y, 2));
		double theta = atan2(input.y, input.x);
		double rn = pow(r, translatedBulge);

		return osci::Point(rn * cos(theta), rn * sin(theta), input.z);
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<BulgeEffect>(),
			new osci::EffectParameter("Bulge", "Applies a bulge that makes the centre of the image larger, and squishes the edges of the image. This applies a distortion to the audio.", "bulge", VERSION_HINT, 0.5, 0.0, 1.0));
		eff->setIcon(BinaryData::bulge_svg);
		return eff;
	}
};
