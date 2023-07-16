#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"


class EffectComponent : public juce::Component {
public:
    EffectComponent(EffectDetails details);
    EffectComponent(EffectDetails details, bool checkboxVisible);
    EffectComponent(Effect& effect);
    EffectComponent(Effect& effect, bool checkboxVisible);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setCheckboxVisible(bool visible);
    void setComponent(std::shared_ptr<juce::Component> component);

    juce::Slider slider;
    EffectDetails details;
    juce::ToggleButton selected;

private:
    void componentSetup();
    bool checkboxVisible = true;
    juce::Rectangle<int> textBounds;
    std::shared_ptr<juce::Component> component;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
