#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "KnobContainerComponent.h"
#include "LabelledTextBox.h"
#include <osci_render_core/effect/osci_EffectParameter.h>

// Modern settings popup for parameter range editing.
// Used by both EffectComponent and KnobContainerComponent via CallOutBox.
class ParameterSettingsComponent : public juce::Component, private juce::Slider::Listener {
public:
    ParameterSettingsComponent(osci::EffectParameter* parameter,
                               std::function<void()> onRangeChanged = nullptr)
        : param(parameter), rangeChangedCallback(std::move(onRangeChanged))
    {
        hasLfoParams = param->lfoStartPercent != nullptr && param->lfoEndPercent != nullptr;

        // --- Title ---
        titleLabel.setText(param->name, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setFont(juce::Font(13.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);

        // --- Min / Max ---
        addAndMakeVisible(min);
        addAndMakeVisible(max);

        min.textBox.setValue(param->min, juce::dontSendNotification);
        max.textBox.setValue(param->max, juce::dontSendNotification);

        min.textBox.onValueChange = [this]() {
            double minVal = min.textBox.getValue();
            double maxVal = max.textBox.getValue();
            if (minVal >= maxVal) {
                minVal = maxVal - param->step;
                min.textBox.setValue(minVal, juce::dontSendNotification);
            }
            param->min = minVal;
            if (rangeChangedCallback) rangeChangedCallback();
        };

        max.textBox.onValueChange = [this]() {
            double minVal = min.textBox.getValue();
            double maxVal = max.textBox.getValue();
            if (maxVal <= minVal) {
                maxVal = minVal + param->step;
                max.textBox.setValue(maxVal, juce::dontSendNotification);
            }
            param->max = maxVal;
            if (rangeChangedCallback) rangeChangedCallback();
        };

        // --- LFO Start / End (optional) ---
        if (hasLfoParams) {
            setupLfoSlider(lfoStartSlider, lfoStartLabel, "LFO Start", param->lfoStartPercent);
            setupLfoSlider(lfoEndSlider, lfoEndLabel, "LFO End", param->lfoEndPercent);

            lfoStartSlider.onValueChange = [this]() {
                param->lfoStartPercent->setUnnormalisedValueNotifyingHost(lfoStartSlider.getValue());
            };
            lfoEndSlider.onValueChange = [this]() {
                param->lfoEndPercent->setUnnormalisedValueNotifyingHost(lfoEndSlider.getValue());
            };
        }

        // --- Smooth knob ---
        smoothKnob.getKnob().setPopupDisplayEnabled(true, false, this);
        smoothKnob.getKnob().setRange(0.01, 1.0, 0.0001);
        smoothKnob.getKnob().setValue(param->smoothValueChange, juce::dontSendNotification);
        smoothKnob.getKnob().setDoubleClickReturnValue(true, 0.25);
        smoothKnob.getKnob().onValueChange = [this]() {
            param->smoothValueChange = smoothKnob.getKnob().getValue();
        };
        addAndMakeVisible(smoothKnob);
    }

    ~ParameterSettingsComponent() override {
        if (hasLfoParams) {
            lfoStartSlider.removeListener(this);
            lfoEndSlider.removeListener(this);
        }
    }

    int getDesiredWidth() const { return 160; }

    int getDesiredHeight() const {
        int h = kPadding;
        h += kTitleHeight;
        h += kSpacing;
        h += kRowHeight;  // min
        h += kRowSpacing;
        h += kRowHeight;  // max
        if (hasLfoParams) {
            h += kSectionSpacing;
            h += kLfoLabelHeight + kLfoSliderHeight; // lfo start
            h += kRowSpacing;
            h += kLfoLabelHeight + kLfoSliderHeight; // lfo end
        }
        h += kSectionSpacing;
        h += kKnobHeight; // smooth knob container (knob + pill label)
        h += kPadding;
        return h;
    }

    void resized() override {
        auto area = getLocalBounds().reduced(kPadding, kPadding);

        titleLabel.setBounds(area.removeFromTop(kTitleHeight));
        area.removeFromTop(kSpacing);

        // Min / Max rows
        min.setBounds(area.removeFromTop(kRowHeight));
        area.removeFromTop(kRowSpacing);
        max.setBounds(area.removeFromTop(kRowHeight));

        // LFO sliders
        if (hasLfoParams) {
            area.removeFromTop(kSectionSpacing);

            lfoStartLabel.setBounds(area.removeFromTop(kLfoLabelHeight));
            lfoStartSlider.setBounds(area.removeFromTop(kLfoSliderHeight));

            area.removeFromTop(kRowSpacing);

            lfoEndLabel.setBounds(area.removeFromTop(kLfoLabelHeight));
            lfoEndSlider.setBounds(area.removeFromTop(kLfoSliderHeight));
        }

        area.removeFromTop(kSectionSpacing);

        // Smooth knob container — centered
        auto knobArea = area.removeFromTop(kKnobHeight)
                            .withSizeKeepingCentre(kKnobWidth, kKnobHeight);
        smoothKnob.setBounds(knobArea);
    }

    void paint(juce::Graphics& g) override {
        // Separator line above the smooth knob section
        auto lineArea = getLocalBounds().reduced(kPadding + 4, 0);
        int separatorY;
        if (hasLfoParams) {
            separatorY = kPadding + kTitleHeight + kSpacing
                       + kRowHeight + kRowSpacing + kRowHeight
                       + kSectionSpacing
                       + (kLfoLabelHeight + kLfoSliderHeight) * 2 + kRowSpacing
                       + kSectionSpacing / 2;
        } else {
            separatorY = kPadding + kTitleHeight + kSpacing
                       + kRowHeight + kRowSpacing + kRowHeight
                       + kSectionSpacing / 2;
        }
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawHorizontalLine(separatorY, (float)lineArea.getX(), (float)lineArea.getRight());
    }

private:
    static constexpr int kPadding = 10;
    static constexpr int kTitleHeight = 18;
    static constexpr int kSpacing = 8;
    static constexpr int kRowHeight = 40;
    static constexpr int kRowSpacing = 0;
    static constexpr int kSectionSpacing = 12;
    static constexpr int kLfoLabelHeight = 16;
    static constexpr int kLfoSliderHeight = 24;
    static constexpr int kKnobWidth = 70;
    static constexpr int kKnobHeight = 55;

    osci::EffectParameter* param;
    std::function<void()> rangeChangedCallback;
    bool hasLfoParams = false;

    juce::Label titleLabel;
    LabelledTextBox min{"Min"};
    LabelledTextBox max{"Max"};

    juce::Label lfoStartLabel, lfoEndLabel;
    juce::Slider lfoStartSlider, lfoEndSlider;

    KnobContainerComponent smoothKnob { "SMOOTH" };

    void setupLfoSlider(juce::Slider& slider, juce::Label& lbl,
                         const juce::String& name, osci::FloatParameter* sliderParam) {
        lbl.setText(name, juce::dontSendNotification);
        lbl.setJustificationType(juce::Justification::centred);
        lbl.setFont(juce::Font(10.0f, juce::Font::bold));
        lbl.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(lbl);

        slider.setRange(sliderParam->min, sliderParam->max, sliderParam->step);
        slider.setValue(sliderParam->getValueUnnormalised(), juce::dontSendNotification);
        slider.setTextValueSuffix("%");
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        slider.addListener(this);
        addAndMakeVisible(slider);
    }

    // juce::Slider::Listener — gesture forwarding for LFO sliders
    void sliderValueChanged(juce::Slider*) override {}

    void sliderDragStarted(juce::Slider* slider) override {
        if (slider == &lfoStartSlider && param->lfoStartPercent != nullptr)
            beginGesture(param->lfoStartPercent);
        else if (slider == &lfoEndSlider && param->lfoEndPercent != nullptr)
            beginGesture(param->lfoEndPercent);
    }

    void sliderDragEnded(juce::Slider* slider) override {
        if (slider == &lfoStartSlider && param->lfoStartPercent != nullptr)
            endGesture(param->lfoStartPercent);
        else if (slider == &lfoEndSlider && param->lfoEndPercent != nullptr)
            endGesture(param->lfoEndPercent);
    }

    static void beginGesture(juce::AudioProcessorParameter* p) {
        if (p != nullptr && p->getParameterIndex() >= 0)
            p->beginChangeGesture();
    }

    static void endGesture(juce::AudioProcessorParameter* p) {
        if (p != nullptr && p->getParameterIndex() >= 0)
            p->endChangeGesture();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterSettingsComponent)
};
