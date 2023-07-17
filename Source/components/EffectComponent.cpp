#include "EffectComponent.h"

EffectComponent::EffectComponent(Effect& effect, int index) : effect(effect), index(index) {
    componentSetup();
}

EffectComponent::EffectComponent(Effect& effect, int index, bool checkboxVisible) : EffectComponent(effect, index) {
    setCheckboxVisible(checkboxVisible);
}

EffectComponent::EffectComponent(Effect& effect) : EffectComponent(effect, 0) {}

EffectComponent::EffectComponent(Effect& effect, bool checkboxVisible) : EffectComponent(effect) {
    setCheckboxVisible(checkboxVisible);
}

void EffectComponent::componentSetup() {
    addAndMakeVisible(slider);
    addAndMakeVisible(selected);

    EffectDetails details = effect.details[index];

    slider.setRange(details.min, details.max, details.step);
    slider.setValue(details.value, juce::dontSendNotification);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, slider.getTextBoxHeight());

    selected.setToggleState(false, juce::dontSendNotification);

    min.textBox.setValue(details.min, juce::dontSendNotification);
    max.textBox.setValue(details.max, juce::dontSendNotification);

    min.textBox.onValueChange = [this]() {
        effect.details[index].min = min.textBox.getValue();
        slider.setRange(effect.details[index].min, effect.details[index].max, effect.details[index].step);
    };

    max.textBox.onValueChange = [this]() {
        effect.details[index].max = max.textBox.getValue();
        slider.setRange(effect.details[index].min, effect.details[index].max, effect.details[index].step);
    };

    popupLabel.setText(details.name + " Settings", juce::dontSendNotification);
    popupLabel.setJustificationType(juce::Justification::centred);
    popupLabel.setFont(juce::Font(14.0f, juce::Font::bold));
}


EffectComponent::~EffectComponent() {}

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
    g.drawText(effect.details[index].name, textBounds, juce::Justification::left);
}

void EffectComponent::mouseDown(const juce::MouseEvent& event) {
    juce::PopupMenu menu;

    menu.addCustomItem(1, popupLabel, 200, 30, false);
    menu.addCustomItem(2, min, 160, 40, false);
    menu.addCustomItem(3, max, 160, 40, false);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        
    });
}

void EffectComponent::setComponent(std::shared_ptr<juce::Component> component) {
	this->component = component;
    addAndMakeVisible(component.get());
}

void EffectComponent::setCheckboxVisible(bool visible) {
    checkboxVisible = visible;
}
