#include "AboutComponent.h"

AboutComponent::AboutComponent(const void *image, size_t imageSize, juce::String sectionText) {
    addAndMakeVisible(logoComponent);
    addAndMakeVisible(text);

    logo = juce::ImageFileFormat::loadFrom(image, imageSize);
    
    logoComponent.setImage(logo);
    
    text.setMultiLine(true);
    text.setReadOnly(true);
    text.setInterceptsMouseClicks(false, false);
    text.setOpaque(false);
    text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setJustification(juce::Justification(juce::Justification::centred));
    text.setText(sectionText);
}

void AboutComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(10);
    logoComponent.setBounds(area.removeFromTop(110));
    text.setBounds(area);
}
