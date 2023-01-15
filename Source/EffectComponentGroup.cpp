#include "EffectComponentGroup.h"

EffectComponentGroup::EffectComponentGroup(const juce::String& id, const juce::String& name) : id(id) {
	addAndMakeVisible(slider);
	addAndMakeVisible(label);

	label.setText(name, juce::dontSendNotification);
	label.attachToComponent(&slider, true);

	slider.setSliderStyle(juce::Slider::LinearHorizontal);
	slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, slider.getTextBoxHeight());
}

EffectComponentGroup::~EffectComponentGroup() {
}

void EffectComponentGroup::resized() {
	slider.setBounds(0, 0, getWidth() - 100, 20);
}
