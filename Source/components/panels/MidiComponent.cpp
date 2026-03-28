#include "MidiComponent.h"
#include "../../PluginEditor.h"
#include "../../LookAndFeel.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {

    addAndMakeVisible(midiToggle);
    addAndMakeVisible(voicesBar);

    audioProcessor.midiEnabled->addListener(this);

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(midiSettingsButton);
        midiSettingsButton.onClick = [this]() {
            pluginEditor.openAudioSettings();
        };
        addAndMakeVisible(tempoBar);
    }

    handleAsyncUpdate();
}

MidiComponent::~MidiComponent() {
    audioProcessor.midiEnabled->removeListener(this);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    const bool midiOn = audioProcessor.midiEnabled->getBoolValue();
    voicesBar.setVisible(midiOn);
}

void MidiComponent::resized() {
    auto area = getLocalBounds().reduced(5);
    midiToggle.setBounds(area.removeFromLeft(120).translated(0, 1));
    area.removeFromLeft(10);
    voicesBar.setBounds(area.removeFromLeft(80).reduced(0, 3));
    if (midiSettingsButton.isVisible()) {
        midiSettingsButton.setBounds(area.removeFromRight(160));
    }
    if (tempoBar.isVisible()) {
        area.removeFromRight(8);
        tempoBar.setBounds(area.removeFromRight(100).reduced(0, 3));
    }
}

void MidiComponent::paint(juce::Graphics& g) {
    g.setColour(Colours::darker());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), Colours::kPillRadius);
}
