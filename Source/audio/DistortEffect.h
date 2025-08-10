#pragma once
#include <JuceHeader.h>


class DistortEffect : public osci::EffectApplication {
public:
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double /*sampleRate*/) override {
		int flip = index % 2 == 0 ? 1 : -1;
		osci::Point jitter = osci::Point(flip * values[0], flip * values[1], flip * values[2]);
		return input + jitter;
	}

	std::shared_ptr<osci::Effect> build() const override {
		auto eff = std::make_shared<osci::Effect>(
			std::make_shared<DistortEffect>(),
			std::vector<osci::EffectParameter*>{
				new osci::EffectParameter("Distort X", "Distorts the image in the horizontal direction by jittering the audio sample being drawn.", "distortX", VERSION_HINT, 0.0, 0.0, 1.0),
				new osci::EffectParameter("Distort Y", "Distorts the image in the vertical direction by jittering the audio sample being drawn.", "distortY", VERSION_HINT, 0.0, 0.0, 1.0),
				new osci::EffectParameter("Distort Z", "Distorts the depth of the image by jittering the audio sample being drawn.", "distortZ", VERSION_HINT, 0.4, 0.0, 1.0),
			}
		);
		eff->setName("Distort");
		eff->setIcon(BinaryData::distort_svg);
		eff->markLockable(false);
		return eff;
	}
};
