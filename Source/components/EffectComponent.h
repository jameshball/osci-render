#pragma once
#include <JuceHeader.h>

#include "LabelledTextBox.h"
#include "SvgButton.h"

class OscirenderAudioProcessor;

class ModulationUpdateBroadcaster;

class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater, public juce::SettableTooltipClient, private juce::Slider::Listener, public juce::DragAndDropTarget {
public:
    EffectComponent(osci::Effect& effect, int index);
    EffectComponent(osci::Effect& effect);
    ~EffectComponent();

    // Global flag: when true, all EffectComponents draw a highlight border
    // to indicate they are valid modulation drop targets.
    static std::atomic<bool> modAnyDragActive;

    // When non-empty, the EffectComponent with this paramId draws a hover highlight.
    static juce::String highlightedParamId;

    // Modulation range highlight: when hovering a depth indicator, show the modulated range on the slider
    static juce::String modRangeParamId;
    static std::atomic<float> modRangeDepth;
    static std::atomic<bool> modRangeBipolar;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    
    // Slider::Listener callbacks for MIDI learn support
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragStarted(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

    void setRangeEnabled(bool enabled);

    void setComponent(std::shared_ptr<juce::Component> component);

    // DragAndDropTarget overrides for LFO assignment
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    // Callback invoked when an LFO is dropped on this slider.
    // Parameters: lfoIndex, paramId
    std::function<void(int, const juce::String&)> onLfoDropped;

    // Callback invoked when an envelope is dropped on this slider.
    // Parameters: envIndex, paramId
    std::function<void(int, const juce::String&)> onEnvDropped;

    // Struct returned by the LFO modulation query callback.
    struct LfoModInfo {
        bool active = false;           // true if any LFO is assigned to this param
        float modulatedPos = 0.0f;     // pixel position of the current modulated value
        juce::Colour colour;           // blended colour from all assigned LFOs
    };

    // Callback: given a paramId, returns LFO modulation info.
    // Set by the parent that has access to the processor.
    std::function<LfoModInfo(const juce::String& paramId, juce::Slider& slider)> queryLfoModulation;

    // Callback: given a paramId, returns envelope modulation info.
    std::function<LfoModInfo(const juce::String& paramId, juce::Slider& slider)> queryEnvModulation;

    // Shared implementation of LFO modulation query.
    static LfoModInfo computeLfoModulation(OscirenderAudioProcessor& processor,
                                           const juce::String& paramId,
                                           juce::Slider& slider);

    // Shared implementation of envelope modulation query.
    static LfoModInfo computeEnvModulation(OscirenderAudioProcessor& processor,
                                           const juce::String& paramId,
                                           juce::Slider& slider);

    // Update LFO modulation display (call from a parent timer, not per-instance).
    void updateLfoModulation();

    // Wire standard modulation callbacks (LFO + envelope drop, query) to the processor.
    // Call this after construction to enable modulation for this slider.
    void wireModulation(OscirenderAudioProcessor& processor);

    juce::Slider slider;
    juce::Slider lfoSlider;
    osci::Effect& effect;
    int index = 0;
    juce::ComboBox lfo;

    class EffectSettingsComponent : public juce::Component, private juce::Slider::Listener {
    public:
        EffectSettingsComponent(EffectComponent* parent) : parameter(parent->effect.parameters[parent->index]) {
            addAndMakeVisible(popupLabel);
            addAndMakeVisible(min);
            addAndMakeVisible(max);
            addAndMakeVisible(lfoStartLabel);
            addAndMakeVisible(lfoEndLabel);
            addAndMakeVisible(lfoStartSlider);
            addAndMakeVisible(lfoEndSlider);
            addAndMakeVisible(smoothValueChangeLabel);
            addAndMakeVisible(smoothValueChangeSlider);

            min.textBox.setValue(parameter->min, juce::dontSendNotification);
            max.textBox.setValue(parameter->max, juce::dontSendNotification);

            min.textBox.onValueChange = [this, parent]() {
                double minValue = min.textBox.getValue();
                double maxValue = max.textBox.getValue();
                if (minValue >= maxValue) {
                    minValue = maxValue - parameter->step;
                    min.textBox.setValue(minValue, juce::dontSendNotification);
                }
                parameter->min = minValue;
                parent->slider.setRange(parameter->min, parameter->max, parameter->step);
            };

            max.textBox.onValueChange = [this, parent]() {
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
            lfoStartSlider.addListener(this);
            lfoStartSlider.onValueChange = [this]() {
                parameter->lfoStartPercent->setUnnormalisedValueNotifyingHost(lfoStartSlider.getValue());
            };

            lfoEndSlider.setRange(parameter->lfoEndPercent->min, parameter->lfoEndPercent->max, parameter->lfoEndPercent->step);
            lfoEndSlider.setValue(parameter->lfoEndPercent->getValueUnnormalised(), juce::dontSendNotification);
            lfoEndSlider.setTextValueSuffix("%");
            lfoEndSlider.addListener(this);
            lfoEndSlider.onValueChange = [this]() {
                parameter->lfoEndPercent->setUnnormalisedValueNotifyingHost(lfoEndSlider.getValue());
            };

            smoothValueChangeLabel.setText("Smooth Value Change Speed", juce::dontSendNotification);
            smoothValueChangeLabel.setJustificationType(juce::Justification::centred);
            smoothValueChangeLabel.setFont(juce::Font(14.0f, juce::Font::bold));

            smoothValueChangeSlider.setRange(0.01, 1.0, 0.0001);
            smoothValueChangeSlider.setValue(parameter->smoothValueChange, juce::dontSendNotification);
            smoothValueChangeSlider.onValueChange = [this]() {
                parameter->smoothValueChange = smoothValueChangeSlider.getValue();
            };

            popupLabel.setText(parameter->name + " Range", juce::dontSendNotification);
            popupLabel.setJustificationType(juce::Justification::centred);
            popupLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        }
        
        ~EffectSettingsComponent() override {
            lfoStartSlider.removeListener(this);
            lfoEndSlider.removeListener(this);
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
        
        void sliderValueChanged(juce::Slider*) override {}
        
        void sliderDragStarted(juce::Slider* slider) override {
            if (slider == &lfoStartSlider && parameter->lfoStartPercent != nullptr) {
                EffectComponent::beginGestureIfPossible (parameter->lfoStartPercent);
            } else if (slider == &lfoEndSlider && parameter->lfoEndPercent != nullptr) {
                EffectComponent::beginGestureIfPossible (parameter->lfoEndPercent);
            }
        }
        
        void sliderDragEnded(juce::Slider* slider) override {
            if (slider == &lfoStartSlider && parameter->lfoStartPercent != nullptr) {
                EffectComponent::endGestureIfPossible (parameter->lfoStartPercent);
            } else if (slider == &lfoEndSlider && parameter->lfoEndPercent != nullptr) {
                EffectComponent::endGestureIfPossible (parameter->lfoEndPercent);
            }
        }

    private:
        osci::EffectParameter* parameter;
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

    static void beginGestureIfPossible (juce::AudioProcessorParameter* parameter)
    {
        if (parameter != nullptr && parameter->getParameterIndex() >= 0)
            parameter->beginChangeGesture();
    }

    static void endGestureIfPossible (juce::AudioProcessorParameter* parameter)
    {
        if (parameter != nullptr && parameter->getParameterIndex() >= 0)
            parameter->endChangeGesture();
    }

    void setSliderValueIfChanged(osci::FloatParameter* parameter, juce::Slider& slider);
    void setupComponent();
    bool lfoEnabled = true;
    bool sidechainEnabled = true;
    bool modDropHighlight = false;
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

    ModulationUpdateBroadcaster* modBroadcaster = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
