#pragma once
#include <JuceHeader.h>

class BitCrushEffect : public osci::EffectApplication {
public:
	// algorithm from https://www.kvraudio.com/forum/viewtopic.php?t=163880
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double effectScale = juce::jlimit(0.0, 1.0, values[0].load());
        double value = values[1].load();
		// change rage of value from 0-1 to 0.0-0.78
		double rangedValue = value * 0.78;
		double powValue = pow(2.0f, 1.0 - rangedValue) - 1.0;
		double crush = powValue * 12;
		double x = powf(2.0f, crush);
		double quant = 0.5 * x;
		double dequant = 1.0f / quant;
		osci::Point output(dequant * std::round(input.x * quant),
                           dequant * std::round(input.y * quant),
                           dequant * std::round(input.z * quant));
        return (1 - effectScale) * input + effectScale * output;
	}

	std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<BitCrushEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Bit Crush",
                                          "Limits the resolution of points drawn to the screen, making the object look pixelated, and making the audio sound more 'digital' and distorted.",
                                          "bitCrushDryWet", VERSION_HINT, 1.0, 0.0, 1.0),
                new osci::EffectParameter("Bit Crush Strength",
                                          "Constrains the resolution which points are limited to.",
                                          "bitCrush",VERSION_HINT, 0.7, 0.0, 1.0)
        });
		eff->setIcon(BinaryData::bitcrush_svg);
		return eff;
	}
};
