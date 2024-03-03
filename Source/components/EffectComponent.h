#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "LabelledTextBox.h"
#include "SvgButton.h"

class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater, public juce::SettableTooltipClient {
public:
    EffectComponent(OscirenderAudioProcessor& p, Effect& effect, int index);
    EffectComponent(OscirenderAudioProcessor& p, Effect& effect);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void setComponent(std::shared_ptr<juce::Component> component);
    void setSliderOnValueChange();

    juce::Slider slider;
    juce::Slider lfoSlider;
    Effect& effect;
    int index = 0;
    juce::ComboBox lfo;

private:
    const int TEXT_BOX_WIDTH = 70;
    const int SMALL_TEXT_BOX_WIDTH = 50;

    const int TEXT_WIDTH = 120;
    const int SMALL_TEXT_WIDTH = 60;

    void setupComponent();
    bool lfoEnabled = true;
    bool sidechainEnabled = true;
    std::shared_ptr<juce::Component> component;
    OscirenderAudioProcessor& audioProcessor;

    std::unique_ptr<SvgButton> sidechainButton;

    juce::Label popupLabel;
    juce::Label label;
    LabelledTextBox min{"Min"};
    LabelledTextBox max{"Max"};

    SvgButton rangeButton = { "rangeButton", BinaryData::range_svg, juce::Colours::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
