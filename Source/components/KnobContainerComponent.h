#pragma once

#include <JuceHeader.h>
#include "RotaryKnobComponent.h"
#include "SvgButton.h"
#include "LabelledTextBox.h"
#include "ModulationState.h"
#include "../LookAndFeel.h"
#include <osci_render_core/effect/osci_EffectParameter.h>

#ifndef SOSCI
#include "../audio/modulation/ModulationTypes.h"
class OscirenderAudioProcessor;
class ModulationUpdateBroadcaster;
#endif

// Generic container that draws a dark rounded-rectangle background behind a
// RotaryKnobComponent with a label underneath. Reusable for any knob that
// needs a labelled card appearance (e.g. SMOOTH, DELAY, STEREO in LFO panel).
//
// Supports full modulation display, drag-and-drop modulation assignment,
// highlight drawing, and right-click parameter settings — matching the
// polish of EffectComponent.
class KnobContainerComponent : public juce::Component, public juce::DragAndDropTarget,
                                public juce::AudioProcessorParameter::Listener,
                                public juce::AsyncUpdater {
public:
    KnobContainerComponent(const juce::String& labelText) : label(labelText) {
        addAndMakeVisible(knob);
    }

    ~KnobContainerComponent() override;

    RotaryKnobComponent& getKnob() { return knob; }

    // Configure the knob's range, default, suffix, and decimal places from a FloatParameter.
    void bindToParameter(osci::FloatParameter* param, double skewCentre) {
        juce::NormalisableRange<double> range((double)param->min.load(), (double)param->max.load());
        range.setSkewForCentre(skewCentre);
        knob.setNormalisableRange(range);
        knob.setDoubleClickReturnValue(true, (double)param->defaultValue.load());
        knob.setTextValueSuffix(param->getLabel());
        knob.setNumDecimalPlacesToDisplay(3);
    }

    void setLabel(const juce::String& text) { label = text; repaint(); }
    void setAccentColour(juce::Colour c) { knob.setAccentColour(c); }

    // Bind the internal knob to a FloatParameter: sets range, skew, default,
    // initial value, and a default onValueChange that updates the parameter.
    // Callers can override onValueChange afterwards for additional behaviour.
    void bindToParam(osci::FloatParameter* param, double skewCentre = 0.0, int decimalPlaces = 2) {
        if (boundParam)
            boundParam->removeListener(this);
        boundParam = param;
        if (boundParam)
            boundParam->addListener(this);

        double minVal = param->min.load();
        double maxVal = param->max.load();
        juce::NormalisableRange<double> range(minVal, maxVal);
        if (skewCentre > minVal && skewCentre < maxVal)
            range.setSkewForCentre(skewCentre);
        knob.setNormalisableRange(range);
        knob.setDoubleClickReturnValue(true, (double)param->defaultValue.load());
        knob.setNumDecimalPlacesToDisplay(decimalPlaces);
        knob.setValue((double)param->getValueUnnormalised(), juce::dontSendNotification);
        knob.onValueChange = [this, param]() {
            param->setUnnormalisedValueNotifyingHost((float)knob.getValue());
        };
    }

    // Bind to an EffectParameter (extends FloatParameter with modulation support).
    // This enables the right-click settings popup.
    void bindToEffectParam(osci::EffectParameter* param, double skewCentre = 0.0, int decimalPlaces = 2) {
        effectParam = param;
        bindToParam(param, skewCentre, decimalPlaces);
    }

    // Lightweight re-bind: switch the underlying parameter without reconfiguring
    // range formatting or suffix. Use when switching between same-shaped params
    // (e.g. LFO source tabs that share the same knob layout).
    void rebindParam(osci::FloatParameter* param) {
        if (boundParam == param) return;
        if (boundParam)
            boundParam->removeListener(this);
        boundParam = param;
        if (boundParam)
            boundParam->addListener(this);

        double minVal = param->min.load();
        double maxVal = param->max.load();
        knob.setRange(minVal, maxVal, param->step.load());
        knob.setDoubleClickReturnValue(true, (double)param->defaultValue.load());
        knob.setValue((double)param->getValueUnnormalised(), juce::dontSendNotification);
        knob.onValueChange = [this, param]() {
            param->setUnnormalisedValueNotifyingHost((float)knob.getValue());
        };
    }

    void enablementChanged() override {
        knob.setEnabled(isEnabled());
    }

    // --- AudioProcessorParameter::Listener ---
    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}

    void handleAsyncUpdate() override {
        if (boundParam) {
            double current = knob.getValue();
            double paramVal = boundParam->getValueUnnormalised();
            // Only update if meaningfully different to preserve slider precision
            if (std::abs(current - paramVal) > 0.0001) {
                knob.setValue(paramVal, juce::dontSendNotification);
            }
            // Update range in case it changed via settings popup
            knob.setRange(boundParam->min.load(), boundParam->max.load(), boundParam->step.load());
        }
    }

    // --- DragAndDropTarget for modulation assignment ---
    bool isInterestedInDragSource(const SourceDetails& details) override {
#ifndef SOSCI
        return ModDrag::isModDrag(details.description.toString());
#else
        return false;
#endif
    }

    void itemDragEnter(const SourceDetails&) override {
        modDropHighlight = true;
        repaint();
    }

    void itemDragExit(const SourceDetails&) override {
        modDropHighlight = false;
        repaint();
    }

    void itemDropped(const SourceDetails& details) override {
        modDropHighlight = false;
        ModulationState::anyDragActive.store(false, std::memory_order_relaxed);
        repaint();
#ifndef SOSCI
        if (!boundParam) return;
        juce::String desc = details.description.toString();
        juce::String paramId = boundParam->paramID;

        auto parsed = ModDrag::parse(desc);
        if (!parsed.valid) return;

        for (auto& binding : modBindings) {
            if (binding.dragPrefix == parsed.type && binding.onDropped) {
                binding.onDropped(parsed.index, paramId);
                return;
            }
        }
#endif
    }

#ifndef SOSCI
    // Modulation binding (same as EffectComponent::ModBinding)
    struct ModBinding {
        juce::String dragPrefix;
        juce::String propPrefix;
        std::function<void(int, const juce::String&)> onDropped;
        std::function<void(const juce::String& paramId, juce::Slider& slider, juce::NamedValueSet& props)> query;
    };

    std::vector<ModBinding> modBindings;

    void wireModulation(OscirenderAudioProcessor& processor);

    void updateModulationDisplay() {
        if (!boundParam) return;
        juce::String paramId = boundParam->paramID;
        auto& props = knob.getProperties();
        bool needsRepaint = false;

        for (auto& binding : modBindings) {
            juce::String activeKey = binding.propPrefix + "_active";
            if (binding.query) {
                binding.query(paramId, knob, props);
                if ((bool)props.getWithDefault(activeKey, false))
                    needsRepaint = true;
            }
        }

        if (needsRepaint)
            knob.repaint();
    }
#else
    void updateModulationDisplay() {}
#endif

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isRightButtonDown() && effectParam) {
            showSettingsPopup();
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();

        // Faint rounded rectangle behind the label only
        auto labelArea = bounds.removeFromBottom(Colours::kLabelHeight);
        auto labelBg = labelArea.reduced(2.0f, 0.0f);
        g.setColour(Colours::darkerer());
        g.fillRoundedRectangle(labelBg, Colours::kPillRadius);

        // Label text
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f));
        g.drawText(label, labelArea, juce::Justification::centred, false);
    }

    void paintOverChildren(juce::Graphics& g) override {
#ifndef SOSCI
        if (!boundParam) return;
        ModulationState::paintModHighlight(g, *this, modDropHighlight, boundParam->paramID);
#endif
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromBottom(Colours::kLabelHeight);
        knob.setBounds(bounds);
    }

private:
    RotaryKnobComponent knob;
    juce::String label;
    osci::FloatParameter* boundParam = nullptr;
    osci::EffectParameter* effectParam = nullptr;
    bool modDropHighlight = false;

#ifndef SOSCI
    ModulationUpdateBroadcaster* modBroadcaster = nullptr;
#endif

    void showSettingsPopup() {
        if (!effectParam) return;

        // Reuse the same settings popup pattern as EffectComponent
        auto settings = std::make_unique<KnobSettingsComponent>(this);
        settings->setLookAndFeel(&getLookAndFeel());
        settings->setSize(200, 170);
        auto& myBox = juce::CallOutBox::launchAsynchronously(
            std::move(settings), getScreenBounds(), nullptr);
        juce::ignoreUnused(myBox);
    }

    // Settings popup for parameter range editing (mirrors EffectComponent::EffectSettingsComponent)
    class KnobSettingsComponent : public juce::Component {
    public:
        KnobSettingsComponent(KnobContainerComponent* parent)
            : parameter(parent->effectParam), owner(parent) {
            addAndMakeVisible(popupLabel);
            addAndMakeVisible(min);
            addAndMakeVisible(max);
            addAndMakeVisible(smoothLabel);
            addAndMakeVisible(smoothSlider);

            min.textBox.setValue(parameter->min, juce::dontSendNotification);
            max.textBox.setValue(parameter->max, juce::dontSendNotification);

            min.textBox.onValueChange = [this]() {
                double minVal = min.textBox.getValue();
                double maxVal = max.textBox.getValue();
                if (minVal >= maxVal) {
                    minVal = maxVal - parameter->step;
                    min.textBox.setValue(minVal, juce::dontSendNotification);
                }
                parameter->min = minVal;
                owner->knob.setRange(parameter->min, parameter->max, parameter->step);
            };

            max.textBox.onValueChange = [this]() {
                double minVal = min.textBox.getValue();
                double maxVal = max.textBox.getValue();
                if (maxVal <= minVal) {
                    maxVal = minVal + parameter->step;
                    max.textBox.setValue(maxVal, juce::dontSendNotification);
                }
                parameter->max = maxVal;
                owner->knob.setRange(parameter->min, parameter->max, parameter->step);
            };

            smoothLabel.setText("Smooth Value Change Speed", juce::dontSendNotification);
            smoothLabel.setJustificationType(juce::Justification::centred);
            smoothLabel.setFont(juce::Font(14.0f, juce::Font::bold));

            smoothSlider.setRange(0.01, 1.0, 0.0001);
            smoothSlider.setValue(parameter->smoothValueChange, juce::dontSendNotification);
            smoothSlider.onValueChange = [this]() {
                parameter->smoothValueChange = smoothSlider.getValue();
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
            smoothLabel.setBounds(bounds.removeFromTop(20));
            smoothSlider.setBounds(bounds.removeFromTop(40));
        }

    private:
        osci::EffectParameter* parameter;
        KnobContainerComponent* owner;
        juce::Label popupLabel;
        LabelledTextBox min{"Min"};
        LabelledTextBox max{"Max"};
        juce::Label smoothLabel;
        juce::Slider smoothSlider;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobContainerComponent)
};
