#include "AboutComponent.h"

AboutComponent::AboutComponent(const void *image, size_t imageSize, juce::String sectionText, int port) {
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

    if (port > 0) {
        addAndMakeVisible(portText);
        
        // TODO: Integrate this better
        portText.setMultiLine(false);
        portText.setReadOnly(true);
        portText.setInterceptsMouseClicks(false, false);
        portText.setOpaque(false);
        portText.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        portText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        portText.setJustification(juce::Justification(juce::Justification::centred));
        portText.setText(juce::String("Blender Port: ") + juce::String(port));
    }
}

void AboutComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(10);
    logoComponent.setBounds(area.removeFromTop(110));
    portText.setBounds(area.removeFromBottom(20).removeFromTop(15));
    text.setBounds(area);
}
