#pragma once
#include <JuceHeader.h>

#include "LabelledTextBox.h"
#include "SvgButton.h"

class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater, public juce::SettableTooltipClient {
public:
    EffectComponent(osci::Effect& effect, int index);
    EffectComponent(osci::Effect& effect);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void setRangeEnabled(bool enabled);

    void setComponent(std::shared_ptr<juce::Component> component);

    juce::Slider slider;
    juce::Slider lfoSlider;
    osci::Effect& effect;
    int index = 0;
    juce::ComboBox lfo;

    class EffectSettingsComponent : public juce::Component {
    public:
        EffectSettingsComponent(EffectComponent* parent) {
            addAndMakeVisible(popupLabel);
            addAndMakeVisible(min);
            addAndMakeVisible(max);
            addAndMakeVisible(lfoStartLabel);
            addAndMakeVisible(lfoEndLabel);
            addAndMakeVisible(lfoStartSlider);
            addAndMakeVisible(lfoEndSlider);
            addAndMakeVisible(smoothValueChangeLabel);
            addAndMakeVisible(smoothValueChangeSlider);

            osci::EffectParameter* parameter = parent->effect.parameters[parent->index];

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

            lfoStartLabel.setText("LFO Start", juce::dontSendNotification);
            lfoStartLabel.setJustificationType(juce::Justification::centred);
            lfoStartLabel.setFont(juce::Font(14.0f, juce::Font::bold));

            lfoEndLabel.setText("LFO End", juce::dontSendNotification);
            lfoEndLabel.setJustificationType(juce::Justification::centred);
            lfoEndLabel.setFont(juce::Font(14.0f, juce::Font::bold));

            lfoStartSlider.setRange(parameter->lfoStartPercent->min, parameter->lfoStartPercent->max, parameter->lfoStartPercent->step);
            lfoStartSlider.setValue(parameter->lfoStartPercent->getValueUnnormalised(), juce::dontSendNotification);
            lfoStartSlider.setTextValueSuffix("%");
            lfoStartSlider.onValueChange = [this, parameter]() {
                parameter->lfoStartPercent->setUnnormalisedValueNotifyingHost(lfoStartSlider.getValue());
            };

            lfoEndSlider.setRange(parameter->lfoEndPercent->min, parameter->lfoEndPercent->max, parameter->lfoEndPercent->step);
            lfoEndSlider.setValue(parameter->lfoEndPercent->getValueUnnormalised(), juce::dontSendNotification);
            lfoEndSlider.setTextValueSuffix("%");
            lfoEndSlider.onValueChange = [this, parameter]() {
                parameter->lfoEndPercent->setUnnormalisedValueNotifyingHost(lfoEndSlider.getValue());
            };

            smoothValueChangeLabel.setText("Smooth Value Change Speed", juce::dontSendNotification);
            smoothValueChangeLabel.setJustificationType(juce::Justification::centred);
            smoothValueChangeLabel.setFont(juce::Font(14.0f, juce::Font::bold));

            smoothValueChangeSlider.setRange(0.01, 1.0, 0.0001);
            smoothValueChangeSlider.setValue(parameter->smoothValueChange, juce::dontSendNotification);
            smoothValueChangeSlider.onValueChange = [this, parameter]() {
                parameter->smoothValueChange = smoothValueChangeSlider.getValue();
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
            lfoStartLabel.setBounds(bounds.removeFromTop(20));
            lfoStartSlider.setBounds(bounds.removeFromTop(40));
            lfoEndLabel.setBounds(bounds.removeFromTop(20));
            lfoEndSlider.setBounds(bounds.removeFromTop(40));
            smoothValueChangeLabel.setBounds(bounds.removeFromTop(20));
            smoothValueChangeSlider.setBounds(bounds.removeFromTop(40));
        }

    private:
        juce::Label popupLabel;
        LabelledTextBox min{"Min"};
        LabelledTextBox max{"Max"};
        juce::Label lfoStartLabel;
        juce::Label lfoEndLabel;
        juce::Slider lfoStartSlider;
        juce::Slider lfoEndSlider;
        juce::Label smoothValueChangeLabel;
        juce::Slider smoothValueChangeSlider;
    };

    std::function<void()> updateToggleState;

private:
    const int TEXT_BOX_WIDTH = 70;
    const int SMALL_TEXT_BOX_WIDTH = 50;

    const int TEXT_WIDTH = 120;
    const int SMALL_TEXT_WIDTH = 90;

    void setSliderValueIfChanged(osci::FloatParameter* parameter, juce::Slider& slider);
    void setupComponent();
    bool lfoEnabled = true;
    bool sidechainEnabled = true;
    std::shared_ptr<juce::Component> component;

    std::unique_ptr<SvgButton> sidechainButton;

    SvgButton linkButton = SvgButton(effect.parameters[index]->name + " Link",
        BinaryData::link_svg,
        juce::Colours::white,
        juce::Colours::red,
        nullptr,
        BinaryData::link_svg
    );

    juce::Label label;

    SvgButton settingsButton = {"settingsButton", BinaryData::cog_svg, juce::Colours::white};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
