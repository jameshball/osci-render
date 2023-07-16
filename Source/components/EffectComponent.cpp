#include "EffectComponent.h"

EffectComponent::EffectComponent(EffectDetails details) : details(details) {
    componentSetup();
    slider.setRange(details.min, details.max, details.step);
    slider.setValue(details.value, juce::dontSendNotification);
}

EffectComponent::EffectComponent(EffectDetails details, bool checkboxVisible) : EffectComponent(details) {
	setCheckboxVisible(checkboxVisible);
}

EffectComponent::EffectComponent(Effect& effect) : EffectComponent(effect.getDetails()[0]) {}

EffectComponent::EffectComponent(Effect& effect, bool checkboxVisible) : EffectComponent(effect) {
    setCheckboxVisible(checkboxVisible);
}

void EffectComponent::componentSetup() {
    addAndMakeVisible(slider);
    addAndMakeVisible(selected);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, slider.getTextBoxHeight());

    selected.setToggleState(false, juce::dontSendNotification);
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
    g.drawText(details.name, textBounds, juce::Justification::left);
}

void EffectComponent::setComponent(std::shared_ptr<juce::Component> component) {
	this->component = component;
    addAndMakeVisible(component.get());
}

void EffectComponent::setCheckboxVisible(bool visible) {
    checkboxVisible = visible;
}
