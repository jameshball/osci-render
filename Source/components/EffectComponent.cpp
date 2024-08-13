#include "EffectComponent.h"
#include "../LookAndFeel.h"

EffectComponent::EffectComponent(OscirenderAudioProcessor& p, Effect& effect, int index) : effect(effect), index(index), audioProcessor(p) {
    addAndMakeVisible(slider);
    addChildComponent(lfoSlider);
    addAndMakeVisible(lfo);
    addAndMakeVisible(label);
    addAndMakeVisible(rangeButton);

    sidechainEnabled = effect.parameters[index]->sidechain != nullptr;
    if (sidechainEnabled) {
        sidechainButton = std::make_unique<SvgButton>(effect.parameters[index]->name, BinaryData::microphone_svg, juce::Colours::white, juce::Colours::red, effect.parameters[index]->sidechain);
        sidechainButton->setTooltip("When enabled, the volume of the input audio controls the value of the slider, acting like a sidechain effect.");
        addAndMakeVisible(*sidechainButton);
    }

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, TEXT_BOX_WIDTH, slider.getTextBoxHeight());

    lfoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lfoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, TEXT_BOX_WIDTH, lfoSlider.getTextBoxHeight());
    lfoSlider.setTextValueSuffix("Hz");
    lfoSlider.setColour(sliderThumbOutlineColourId, juce::Colour(0xff00ff00));

    label.setFont(juce::Font(13.0f));

    lfo.addItem("Static", static_cast<int>(LfoType::Static));
    lfo.addItem("Sine", static_cast<int>(LfoType::Sine));
    lfo.addItem("Square", static_cast<int>(LfoType::Square));
    lfo.addItem("Seesaw", static_cast<int>(LfoType::Seesaw));
    lfo.addItem("Triangle", static_cast<int>(LfoType::Triangle));
    lfo.addItem("Sawtooth", static_cast<int>(LfoType::Sawtooth));
    lfo.addItem("Reverse Sawtooth", static_cast<int>(LfoType::ReverseSawtooth));
    lfo.addItem("Noise", static_cast<int>(LfoType::Noise));

    rangeButton.setTooltip("Click to change the range of the slider.");

    rangeButton.onClick = [this] {
        juce::PopupMenu menu;

        menu.addCustomItem(1, popupLabel, 200, 30, false);
        menu.addCustomItem(2, min, 160, 40, false);
        menu.addCustomItem(3, max, 160, 40, false);

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {});
    };

    effect.addListener(index, this);
    setupComponent();
}

EffectComponent::EffectComponent(OscirenderAudioProcessor& p, Effect& effect) : EffectComponent(p, effect, 0) {}

void EffectComponent::setupComponent() {
    EffectParameter* parameter = effect.parameters[index];

    setEnabled(effect.enabled == nullptr || effect.enabled->getBoolValue());

    setTooltip(parameter->description);
    label.setText(parameter->name, juce::dontSendNotification);
    label.setInterceptsMouseClicks(false, false);

    slider.setRange(parameter->min, parameter->max, parameter->step);
    slider.setValue(parameter->getValueUnnormalised(), juce::dontSendNotification);

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
        lfoSlider.setSkewFactorFromMidPoint(parameter->lfoRate->min + 0.2 * (parameter->lfoRate->max - parameter->lfoRate->min));

        if (lfo.getSelectedId() == static_cast<int>(LfoType::Static)) {
            lfoSlider.setVisible(false);
            slider.setVisible(true);
        } else {
            lfoSlider.setVisible(true);
            slider.setVisible(false);
        }

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

    popupLabel.setText(parameter->name + " Range", juce::dontSendNotification);
    popupLabel.setJustificationType(juce::Justification::centred);
    popupLabel.setFont(juce::Font(14.0f, juce::Font::bold));

    if (sidechainEnabled) {
        sidechainButton->onClick = [this] {
            effect.parameters[index]->sidechain->setBoolValueNotifyingHost(!effect.parameters[index]->sidechain->getBoolValue());
        };
    }
    

    if (sidechainEnabled && effect.parameters[index]->sidechain->getBoolValue()) {
        slider.setEnabled(false);
        slider.setColour(sliderThumbOutlineColourId, juce::Colour(0xffff0000));
        slider.setTooltip("Sidechain effect applied - click the microphone icon to disable this.");
    } else {
        slider.setEnabled(true);
        slider.setColour(sliderThumbOutlineColourId, findColour(sliderThumbOutlineColourId));
        slider.setTooltip("");
    }
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

    if (sidechainEnabled) {
        sidechainButton->setBounds(bounds.removeFromRight(20));
    }

    bool drawingSmall = bounds.getWidth() < 3.5 * TEXT_WIDTH;

    if (lfoEnabled) {
        lfo.setBounds(bounds.removeFromRight(drawingSmall ? 70 : 100).reduced(0, 5));
    }

    rangeButton.setBounds(bounds.removeFromRight(20));

    bounds.removeFromLeft(5);

    label.setBounds(bounds.removeFromLeft(drawingSmall ? SMALL_TEXT_WIDTH : TEXT_WIDTH));
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, drawingSmall ? SMALL_TEXT_BOX_WIDTH : TEXT_BOX_WIDTH, slider.getTextBoxHeight());
    lfoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, drawingSmall ? SMALL_TEXT_BOX_WIDTH : TEXT_BOX_WIDTH, lfoSlider.getTextBoxHeight());

    slider.setBounds(bounds);
    lfoSlider.setBounds(bounds);
}

void EffectComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto length = effect.parameters.size();
    auto isEnd = index == length - 1;
    auto isStart = index == 0;
    g.setColour(findColour(effectComponentBackgroundColourId, true));
    juce::Path path;
    path.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS, false, isStart, false, isEnd);
    g.fillPath(path);
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

void EffectComponent::setSliderOnValueChange() {
    slider.onValueChange = [this] {
        effect.setValue(index, slider.getValue());
    };
}
