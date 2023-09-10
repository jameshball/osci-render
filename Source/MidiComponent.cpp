#include "MidiComponent.h"
#include "PluginEditor.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(midiToggle);
    addAndMakeVisible(keyboard);

    midiToggle.setToggleState(audioProcessor.midiEnabled->getBoolValue(), juce::dontSendNotification);
    
    midiToggle.onClick = [this]() {
        audioProcessor.midiEnabled->setBoolValueNotifyingHost(midiToggle.getToggleState());
    };
}


void MidiComponent::resized() {
    auto area = getLocalBounds().reduced(5);
    midiToggle.setBounds(area.removeFromTop(50));
    keyboard.setBounds(area.removeFromBottom(100));
}

void MidiComponent::paint(juce::Graphics& g) {
    auto rect = getLocalBounds().reduced(5);
    g.setColour(getLookAndFeel().findColour(groupComponentBackgroundColourId));
    g.fillRect(rect);
}
