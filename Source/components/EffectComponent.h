#pragma once
#include <JuceHeader.h>
#include "../audio/Effect.h"
#include "LabelledTextBox.h"
#include "SvgButton.h"

class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater, public juce::SettableTooltipClient {
public:
    EffectComponent(Effect& effect, int index);
    EffectComponent(Effect& effect);
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
    
    class EffectRangeComponent : public juce::Component {
    public:
        EffectRangeComponent(EffectComponent* parent) {
            addAndMakeVisible(popupLabel);
            addAndMakeVisible(min);
            addAndMakeVisible(max);
            
            EffectParameter* parameter = parent->effect.parameters[parent->index];
            
            min.textBox.setValue(parameter->min, juce::dontSendNotification);
            max.textBox.setValue(parameter->max, juce::dontSendNotification);

            min.textBox.onValueChange = [this, parameter, parent]() {
                double minValue = min.textBox.getValue();
                double maxValue = max.textBox.getValue();
                if (minValue >= maxValue) {
                    minValue = maxValue - parameter->step;
                    min.textBox.setValue(minValue, juce::dontSendNotification);
                }
                parameter->min = minValue;
                parent->slider.setRange(parameter->min, parameter->max, parameter->step);
            };

            max.textBox.onValueChange = [this, parameter, parent]() {
                double minValue = min.textBox.getValue();
                double maxValue = max.textBox.getValue();
                if (maxValue <= minValue) {
                    maxValue = minValue + parameter->step;
                    max.textBox.setValue(maxValue, juce::dontSendNotification);
                }
                parameter->max = maxValue;
                parent->slider.setRange(parameter->min, parameter->max, parameter->step);
            };

            popupLabel.setText(parameter->name + " Range", juce::dontSendNotification);
            popupLabel.setJustificationType(juce::Justification::centred);
            popupLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        }
        
        void resized() override {
            auto bounds = getLocalBounds();
            popupLabel.setBounds(bounds.removeFromTop(30));
            min.setBounds(bounds.removeFromTop(40));
            max.setBounds(bounds.removeFromTop(40));
        }
        
    private:
        juce::Label popupLabel;
        LabelledTextBox min{"Min"};
        LabelledTextBox max{"Max"};
    };

private:
    const int TEXT_BOX_WIDTH = 70;
    const int SMALL_TEXT_BOX_WIDTH = 50;

    const int TEXT_WIDTH = 120;
    const int SMALL_TEXT_WIDTH = 60;

    void setupComponent();
    bool lfoEnabled = true;
    bool sidechainEnabled = true;
    std::shared_ptr<juce::Component> component;

    std::unique_ptr<SvgButton> sidechainButton;

    juce::Label label;

    SvgButton rangeButton = { "rangeButton", BinaryData::range_svg, juce::Colours::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
