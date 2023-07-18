#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "LabelledTextBox.h"


class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater {
public:
    EffectComponent(Effect& effect, int index);
    EffectComponent(Effect& effect, int index, bool checkboxVisible);
    EffectComponent(Effect& effect);
    EffectComponent(Effect& effect, bool checkboxVisible);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void setCheckboxVisible(bool visible);
    void setComponent(std::shared_ptr<juce::Component> component);

    juce::Slider slider;
    Effect& effect;
    int index;
    juce::ToggleButton selected;

private:
    void setupComponent();
    bool checkboxVisible = true;
    juce::Rectangle<int> textBounds;
    std::shared_ptr<juce::Component> component;

    juce::Label popupLabel;
    LabelledTextBox min{"Min"};
    LabelledTextBox max{"Max"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
