#include "EffectComponent.h"
#include "../LookAndFeel.h"

EffectComponent::EffectComponent(OscirenderAudioProcessor& p, Effect& effect, int index) : effect(effect), index(index), audioProcessor(p) {
    addAndMakeVisible(slider);
    addChildComponent(lfoSlider);
    addAndMakeVisible(lfo);

    lfo.addItem("Static", static_cast<int>(LfoType::Static));
    lfo.addItem("Sine", static_cast<int>(LfoType::Sine));
    lfo.addItem("Square", static_cast<int>(LfoType::Square));
    lfo.addItem("Seesaw", static_cast<int>(LfoType::Seesaw));
    lfo.addItem("Triangle", static_cast<int>(LfoType::Triangle));
    lfo.addItem("Sawtooth", static_cast<int>(LfoType::Sawtooth));
    lfo.addItem("Reverse Sawtooth", static_cast<int>(LfoType::ReverseSawtooth));
    lfo.addItem("Noise", static_cast<int>(LfoType::Noise));

    // temporarily disabling tooltips
    // setTooltip(effect.getDescription());

    effect.addListener(index, this);
    setupComponent();
}

EffectComponent::EffectComponent(OscirenderAudioProcessor& p, Effect& effect) : EffectComponent(p, effect, 0) {}

void EffectComponent::setupComponent() {
    EffectParameter* parameter = effect.parameters[index];

    slider.setRange(parameter->min, parameter->max, parameter->step);
    slider.setValue(parameter->getValueUnnormalised(), juce::dontSendNotification);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, slider.getTextBoxHeight());

    lfoEnabled = parameter->lfo != nullptr && parameter->lfoRate != nullptr;
    if (lfoEnabled) {
        lfo.setSelectedId(parameter->lfo->getValueUnnormalised(), juce::dontSendNotification);

        lfo.onChange = [this]() {
            if (lfo.getSelectedId() != 0) {
                effect.parameters[index]->lfo->setUnnormalisedValueNotifyingHost(lfo.getSelectedId());

                if (lfo.getSelectedId() == static_cast<int>(LfoType::Static)) {
                    lfoSlider.setVisible(false);
                    slider.setVisible(true);
                } else {
                    lfoSlider.setVisible(true);
                    slider.setVisible(false);
                }
            }
        };

        lfoSlider.setRange(parameter->lfoRate->min, parameter->lfoRate->max, parameter->lfoRate->step);
        lfoSlider.setValue(parameter->lfoRate->getValueUnnormalised(), juce::dontSendNotification);

        if (lfo.getSelectedId() == static_cast<int>(LfoType::Static)) {
            lfoSlider.setVisible(false);
            slider.setVisible(true);
        } else {
            lfoSlider.setVisible(true);
            slider.setVisible(false);
        }

        lfoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        lfoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, lfoSlider.getTextBoxHeight());
        lfoSlider.setTextValueSuffix("Hz");
        lfoSlider.setColour(sliderThumbOutlineColourId, juce::Colour(0xff00ff00));

        lfoSlider.onValueChange = [this]() {
            effect.parameters[index]->lfoRate->setUnnormalisedValueNotifyingHost(lfoSlider.getValue());
        };
    }
    
    min.textBox.setValue(parameter->min, juce::dontSendNotification);
    max.textBox.setValue(parameter->max, juce::dontSendNotification);

    min.textBox.onValueChange = [this]() {
        double minValue = min.textBox.getValue();
        double maxValue = max.textBox.getValue();
        if (minValue >= maxValue) {
            minValue = maxValue - effect.parameters[index]->step;
            min.textBox.setValue(minValue, juce::dontSendNotification);
        }
        effect.parameters[index]->min = minValue;
        slider.setRange(effect.parameters[index]->min, effect.parameters[index]->max, effect.parameters[index]->step);
    };

    max.textBox.onValueChange = [this]() {
        double minValue = min.textBox.getValue();
        double maxValue = max.textBox.getValue();
        if (maxValue <= minValue) {
            maxValue = minValue + effect.parameters[index]->step;
            max.textBox.setValue(maxValue, juce::dontSendNotification);
        }
        effect.parameters[index]->max = maxValue;
        slider.setRange(effect.parameters[index]->min, effect.parameters[index]->max, effect.parameters[index]->step);
    };

    popupLabel.setText(parameter->name + " Settings", juce::dontSendNotification);
    popupLabel.setJustificationType(juce::Justification::centred);
    popupLabel.setFont(juce::Font(14.0f, juce::Font::bold));
}


EffectComponent::~EffectComponent() {
    effect.removeListener(index, this);
}

void EffectComponent::resized() {
    auto bounds = getLocalBounds();
    auto componentBounds = bounds.removeFromRight(25);
    if (component != nullptr) {
		component->setBounds(componentBounds);
	}

    if (lfoEnabled) {
        lfo.setBounds(bounds.removeFromRight(100).reduced(5));
    }

    textBounds = bounds.removeFromLeft(120);
    textBounds.removeFromLeft(5);
    slider.setBounds(bounds);
    lfoSlider.setBounds(bounds);
}

void EffectComponent::paint(juce::Graphics& g) {
    g.fillAll(findColour(effectComponentBackgroundColourId));
    g.setColour(juce::Colours::white);
    g.drawText(effect.parameters[index]->name, textBounds, juce::Justification::left);
}

void EffectComponent::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;

        menu.addCustomItem(1, popupLabel, 200, 30, false);
        menu.addCustomItem(2, min, 160, 40, false);
        menu.addCustomItem(3, max, 160, 40, false);

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {});
    }
}

void EffectComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void EffectComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void EffectComponent::handleAsyncUpdate() {
    setupComponent();
    juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
    if (effect.getId().contains("lua")) {
        effect.apply();
    }
}

void EffectComponent::setComponent(std::shared_ptr<juce::Component> component) {
	this->component = component;
    addAndMakeVisible(component.get());
}
