#include "EffectComponent.h"

#include "../LookAndFeel.h"

EffectComponent::EffectComponent(osci::Effect& effect, int index) : effect(effect), index(index) {
    addAndMakeVisible(slider);
    addChildComponent(lfoSlider);
    addAndMakeVisible(lfo);
    addAndMakeVisible(label);
    addAndMakeVisible(settingsButton);

    sidechainEnabled = effect.parameters[index]->sidechain != nullptr;
    if (sidechainEnabled) {
        sidechainButton = std::make_unique<SvgButton>(effect.parameters[index]->name, BinaryData::microphone_svg, juce::Colours::white, juce::Colours::red, effect.parameters[index]->sidechain);
        sidechainButton->setTooltip("When enabled, the volume of the input audio controls the value of the slider, acting like a sidechain effect.");
        addAndMakeVisible(*sidechainButton);
    }

    if (effect.linked != nullptr && index == 0) {
        linkButton.setTooltip("When enabled, parameters are linked and changes to one parameter will affect all parameters.");
        linkButton.onClick = [this]() {
            if (this->effect.linked != nullptr) {
                this->effect.linked->setBoolValueNotifyingHost(!this->effect.linked->getBoolValue());
            }
        };
        addAndMakeVisible(linkButton);
    }

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, TEXT_BOX_WIDTH, slider.getTextBoxHeight());
    if (effect.parameters[index]->step == 1.0) {
        slider.setNumDecimalPlacesToDisplay(0);
    } else {
        slider.setNumDecimalPlacesToDisplay(4);
    }

    lfoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lfoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, TEXT_BOX_WIDTH, lfoSlider.getTextBoxHeight());
    lfoSlider.setTextValueSuffix("Hz");
    lfoSlider.setColour(sliderThumbOutlineColourId, juce::Colour(0xff00ff00));
    lfoSlider.setNumDecimalPlacesToDisplay(3);

    label.setFont(juce::Font(14.0f));

    lfo.addItem("Static", static_cast<int>(osci::LfoType::Static));
    lfo.addItem("Sine", static_cast<int>(osci::LfoType::Sine));
    lfo.addItem("Square", static_cast<int>(osci::LfoType::Square));
    lfo.addItem("Seesaw", static_cast<int>(osci::LfoType::Seesaw));
    lfo.addItem("Triangle", static_cast<int>(osci::LfoType::Triangle));
    lfo.addItem("Sawtooth", static_cast<int>(osci::LfoType::Sawtooth));
    lfo.addItem("Reverse Sawtooth", static_cast<int>(osci::LfoType::ReverseSawtooth));
    lfo.addItem("Noise", static_cast<int>(osci::LfoType::Noise));

    settingsButton.setTooltip("Click to change the slider settings, including range.");

    settingsButton.onClick = [this] {
        auto settings = std::make_unique<EffectSettingsComponent>(this);
        settings->setLookAndFeel(&getLookAndFeel());
        settings->setSize(200, 290);
        auto& myBox = juce::CallOutBox::launchAsynchronously(std::move(settings), settingsButton.getScreenBounds(), nullptr);
    };

    effect.addListener(index, this);
    setupComponent();
}

EffectComponent::EffectComponent(osci::Effect& effect) : EffectComponent(effect, 0) {}

void EffectComponent::setSliderValueIfChanged(osci::FloatParameter* parameter, juce::Slider& slider) {
    juce::String newSliderValue = juce::String(parameter->getValueUnnormalised(), 3);
    juce::String oldSliderValue = juce::String((float)slider.getValue(), 3);

    // only set the slider value if the parameter value is different so that we prefer the more
    // precise slider value.
    if (newSliderValue != oldSliderValue) {
        slider.setValue(parameter->getValueUnnormalised(), juce::dontSendNotification);
    }
}

void EffectComponent::setupComponent() {
    osci::EffectParameter* parameter = effect.parameters[index];

    setEnabled(effect.enabled == nullptr || effect.enabled->getBoolValue());

    if (effect.linked != nullptr && index == 0) {
        linkButton.setToggleState(effect.linked->getBoolValue(), juce::dontSendNotification);

        if (effect.linked->getBoolValue()) {
            for (int i = 1; i < effect.parameters.size(); i++) {
                effect.setValue(i, effect.parameters[0]->getValueUnnormalised());
            }
        }
    }

    if (updateToggleState != nullptr) {
        updateToggleState();
    }

    setTooltip(parameter->description);
    label.setText(parameter->name, juce::dontSendNotification);
    label.setInterceptsMouseClicks(false, false);

    slider.setRange(parameter->min, parameter->max, parameter->step);
    setSliderValueIfChanged(parameter, slider);
    slider.setDoubleClickReturnValue(true, parameter->defaultValue);

    // Set the new slider value change handler
    slider.onValueChange = [this] {
        // Update the effect parameter with this slider's value
        effect.setValue(index, slider.getValue());

        if (effect.linked != nullptr && effect.linked->getBoolValue()) {
            for (int i = 0; i < effect.parameters.size(); i++) {
                if (i != index) {
                    effect.setValue(i, slider.getValue());
                }
            }
        }
    };

    lfoEnabled = parameter->lfo != nullptr && parameter->lfoRate != nullptr;
    if (lfoEnabled) {
        lfo.setSelectedId(parameter->lfo->getValueUnnormalised(), juce::dontSendNotification);

        lfo.onChange = [this]() {
            if (lfo.getSelectedId() != 0) {
                effect.parameters[index]->lfo->setUnnormalisedValueNotifyingHost(lfo.getSelectedId());

                if (lfo.getSelectedId() == static_cast<int>(osci::LfoType::Static)) {
                    lfoSlider.setVisible(false);
                    slider.setVisible(true);
                } else {
                    lfoSlider.setVisible(true);
                    slider.setVisible(false);
                }
            }
        };

        lfoSlider.setRange(parameter->lfoRate->min, parameter->lfoRate->max, parameter->lfoRate->step);
        setSliderValueIfChanged(parameter->lfoRate, lfoSlider);
        lfoSlider.setValue(parameter->lfoRate->getValueUnnormalised(), juce::dontSendNotification);
        lfoSlider.setSkewFactorFromMidPoint(parameter->lfoRate->min + 0.1 * (parameter->lfoRate->max - parameter->lfoRate->min));
        lfoSlider.setDoubleClickReturnValue(true, 1.0);

        if (lfo.getSelectedId() == static_cast<int>(osci::LfoType::Static)) {
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

    if (effect.linked != nullptr) {
        linkButton.setBounds(componentBounds);
    } else if (component != nullptr) {
        component->setBounds(componentBounds);
    }

    if (sidechainEnabled) {
        sidechainButton->setBounds(bounds.removeFromRight(20));
    }

    if (settingsButton.isVisible()) {
        settingsButton.setBounds(bounds.removeFromRight(20));
    }

    bool drawingSmall = bounds.getWidth() < 3.5 * TEXT_WIDTH;

    if (lfoEnabled) {
        lfo.setBounds(bounds.removeFromRight(drawingSmall ? 70 : 100).reduced(0, 5));
    }

    bounds.removeFromRight(2);

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
    if (auto* parent = getParentComponent()) {
        parent->repaint();
    }
}

void EffectComponent::setRangeEnabled(bool enabled) {
    settingsButton.setVisible(enabled);
}

void EffectComponent::setComponent(std::shared_ptr<juce::Component> component) {
    this->component = component;
    addAndMakeVisible(component.get());
}
