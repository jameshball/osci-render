#include "AboutComponent.h"

AboutComponent::AboutComponent() {
    addAndMakeVisible(logoComponent);
    addAndMakeVisible(text);
    
    logoComponent.setImage(logo);
    
    text.setMultiLine(true);
    text.setReadOnly(true);
    text.setInterceptsMouseClicks(false, false);
    text.setOpaque(false);
    text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setJustification(juce::Justification(juce::Justification::centred));
    text.setText(
        juce::String(ProjectInfo::projectName) + " by " + ProjectInfo::companyName + "\n"
        "Version " + ProjectInfo::versionString + "\n\n"
        "A huge thank you to:\n"
        "DJ_Level_3, for contributing several features to osci-render\n"
        "BUS ERROR Collective, for providing the source code for the Hilligoss encoder\n"
        "All the community, for suggesting features and reporting issues!\n\n"
        "I am open for commissions! Email me at james@ball.sh."
    );
}

void AboutComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(10);
    logoComponent.setBounds(area.removeFromTop(110));
    text.setBounds(area);
}
