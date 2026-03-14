#include "MidiComponent.h"
#include "PluginEditor.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor), tempoComponent(p) {

    addAndMakeVisible(midiToggle);
    addAndMakeVisible(voicesSlider);
    addAndMakeVisible(voicesLabel);

    voicesSlider.setRange(1, 16, 1);
    voicesSlider.setValue(audioProcessor.voices->getValueUnnormalised(), juce::dontSendNotification);
    voicesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);

    voicesLabel.setText("Voices", juce::dontSendNotification);
    voicesLabel.attachToComponent(&voicesSlider, true);
    voicesLabel.setTooltip("Number of voices for the synth to use. Larger numbers will use more CPU, and may cause audio glitches.");

    voicesSlider.onValueChange = [this]() {
        audioProcessor.voices->setUnnormalisedValueNotifyingHost(voicesSlider.getValue());
    };

    audioProcessor.voices->addListener(this);
    audioProcessor.midiEnabled->addListener(this);

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(midiSettingsButton);
        midiSettingsButton.onClick = [this]() {
            pluginEditor.openAudioSettings();
        };
        addAndMakeVisible(tempoComponent);
    }

    handleAsyncUpdate();
}

MidiComponent::~MidiComponent() {
    audioProcessor.voices->removeListener(this);
    audioProcessor.midiEnabled->removeListener(this);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    voicesSlider.setValue(audioProcessor.voices->getValueUnnormalised(), juce::dontSendNotification);

    const bool midiOn = audioProcessor.midiEnabled->getBoolValue();
    voicesSlider.setVisible(midiOn);
    voicesLabel.setVisible(midiOn);
}

void MidiComponent::resized() {
    auto area = getLocalBounds().reduced(5);
    auto topRow = area.removeFromTop(30);
    midiToggle.setBounds(topRow.removeFromLeft(120).translated(0, 1));
    topRow.removeFromLeft(80);
    voicesSlider.setBounds(topRow.removeFromLeft(250));
    if (midiSettingsButton.isVisible()) {
        midiSettingsButton.setBounds(topRow.removeFromRight(160));
    }
    if (tempoComponent.isVisible()) {
        topRow.removeFromRight(8);
        tempoComponent.setBounds(topRow.removeFromRight(100).reduced(0, 3));
    }
}

void MidiComponent::paint(juce::Graphics& g) {
}
