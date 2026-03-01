#include "LfoModulationHelper.h"
#include "../../PluginProcessor.h"

LfoModulationHelper::Info LfoModulationHelper::compute(
        OscirenderAudioProcessor& processor,
        const juce::String& paramId,
        juce::Slider& sl) {
    Info info;
    auto assignments = processor.getLfoAssignments();

    float totalR = 0, totalG = 0, totalB = 0;
    int count = 0;
    double currentVal = sl.getValue();
    double sliderMin = sl.getMinimum();
    double sliderMax = sl.getMaximum();
    double range = sliderMax - sliderMin;

    double totalOffset = 0.0;
    for (const auto& a : assignments) {
        if (a.paramId != paramId) continue;

        float lfoVal = processor.getLfoCurrentValue(a.lfoIndex);
        auto colour = LfoComponent::getLfoColour(a.lfoIndex);
        totalR += colour.getFloatRed();
        totalG += colour.getFloatGreen();
        totalB += colour.getFloatBlue();
        count++;

        if (a.bipolar) {
            totalOffset += (lfoVal * 2.0 - 1.0) * a.depth * range * 0.5;
        } else {
            totalOffset += lfoVal * a.depth * range;
        }
    }

    if (count > 0) {
        double modulatedVal = juce::jlimit(sliderMin, sliderMax, currentVal + totalOffset);
        info.active = true;
        info.modulatedPos = (float)sl.getPositionOfValue(modulatedVal);
        info.colour = juce::Colour::fromFloatRGBA(
            totalR / count, totalG / count, totalB / count, 1.0f);
    }
    return info;
}
