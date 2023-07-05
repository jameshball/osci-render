#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"


class EffectComponent : public juce::Component {
public:
    EffectComponent(double min, double max, double step, double value, juce::String name, juce::String id);
    EffectComponent(double min, double max, double step, Effect& effect);
    EffectComponent(double min, double max, double step, Effect& effect, bool checkboxVisible);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setCheckboxVisible(bool visible);
    void addComponent(std::shared_ptr<juce::Component> component);
    void setRightPadding(double padding);

    juce::Slider slider;
    juce::String id;
    juce::String name;
    juce::ToggleButton selected;

private:
    void componentSetup();
    bool checkboxVisible = true;
    juce::Rectangle<int> textBounds;
    std::vector<std::shared_ptr<juce::Component>> components;
    double rightPadding = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
