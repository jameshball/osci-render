#include "EffectComponent.h"

EffectComponent::EffectComponent(Effect& effect, int index) : effect(effect), index(index) {
    addAndMakeVisible(slider);
    addAndMakeVisible(selected);
    effect.addListener(index, this);
    setupComponent();
}

EffectComponent::EffectComponent(Effect& effect, int index, bool checkboxVisible) : EffectComponent(effect, index) {
    setCheckboxVisible(checkboxVisible);
}

EffectComponent::EffectComponent(Effect& effect) : EffectComponent(effect, 0) {}

EffectComponent::EffectComponent(Effect& effect, bool checkboxVisible) : EffectComponent(effect) {
    setCheckboxVisible(checkboxVisible);
}

void EffectComponent::setupComponent() {
    EffectParameter& parameter = effect.parameters[index];

    slider.setRange(parameter.min, parameter.max, parameter.step);
    slider.setValue(parameter.getValueUnnormalised(), juce::dontSendNotification);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, slider.getTextBoxHeight());

    selected.setToggleState(effect.enabled.getValue(), juce::dontSendNotification);

    min.textBox.setValue(parameter.min, juce::dontSendNotification);
    max.textBox.setValue(parameter.max, juce::dontSendNotification);

    min.textBox.onValueChange = [this]() {
        double minValue = min.textBox.getValue();
        double maxValue = max.textBox.getValue();
        if (minValue >= maxValue) {
            minValue = maxValue - effect.parameters[index].step;
            min.textBox.setValue(minValue, juce::dontSendNotification);
        }
        effect.parameters[index].min = minValue;
        slider.setRange(effect.parameters[index].min, effect.parameters[index].max, effect.parameters[index].step);
    };

    max.textBox.onValueChange = [this]() {
        double minValue = min.textBox.getValue();
        double maxValue = max.textBox.getValue();
        if (maxValue <= minValue) {
            maxValue = minValue + effect.parameters[index].step;
            max.textBox.setValue(maxValue, juce::dontSendNotification);
        }
        effect.parameters[index].max = maxValue;
        slider.setRange(effect.parameters[index].min, effect.parameters[index].max, effect.parameters[index].step);
    };

    popupLabel.setText(parameter.name + " Settings", juce::dontSendNotification);
    popupLabel.setJustificationType(juce::Justification::centred);
    popupLabel.setFont(juce::Font(14.0f, juce::Font::bold));
}


EffectComponent::~EffectComponent() {
    effect.removeListener(index, this);
}

void EffectComponent::resized() {
    auto sliderRight = getWidth() - 160;
    auto bounds = getLocalBounds();
    auto componentBounds = bounds.removeFromRight(25);
    if (component != nullptr) {
		component->setBounds(componentBounds);
	}

    slider.setBounds(bounds.removeFromRight(sliderRight));
    if (checkboxVisible) {
        bounds.removeFromLeft(2);
        selected.setBounds(bounds.removeFromLeft(25));
    } else {
        bounds.removeFromLeft(5);
    }
    textBounds = bounds;
}

void EffectComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.drawText(effect.parameters[index].name, textBounds, juce::Justification::left);
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
}

void EffectComponent::setComponent(std::shared_ptr<juce::Component> component) {
	this->component = component;
    addAndMakeVisible(component.get());
}

void EffectComponent::setCheckboxVisible(bool visible) {
    checkboxVisible = visible;
}
