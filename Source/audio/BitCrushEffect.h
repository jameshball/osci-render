#pragma once
#include <JuceHeader.h>

class BitCrushEffect : public osci::EffectApplication {
public:
	// algorithm from https://www.kvraudio.com/forum/viewtopic.php?t=163880
	osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
		double value = values[0];
		// change rage of value from 0-1 to 0.0-0.78
		double rangedValue = value * 0.78;
		double powValue = pow(2.0f, 1.0 - rangedValue) - 1.0;
		double crush = powValue * 12;
		double x = powf(2.0f, crush);
		double quant = 0.5 * x;
		double dequant = 1.0f / quant;
		return osci::Point(dequant * (int)(input.x * quant), dequant * (int)(input.y * quant), dequant * (int)(input.z * quant));
	}
};
