#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"


class EffectComponent : public juce::Component {
public:
    EffectComponent(double min, double max, double step, double value, juce::String name, juce::String id);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setHideCheckbox(bool hide);

    juce::Slider slider;
    juce::String id;
    juce::String name;
    juce::ToggleButton selected;

private:
    bool hideCheckbox = false;
    juce::Rectangle<int> textBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
