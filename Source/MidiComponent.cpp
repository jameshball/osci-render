#include "MidiComponent.h"
#include "PluginEditor.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(midiToggle);
    addAndMakeVisible(keyboard);

    midiToggle.setToggleState(audioProcessor.midiEnabled->getBoolValue(), juce::dontSendNotification);
    
    midiToggle.onClick = [this]() {
        audioProcessor.midiEnabled->setBoolValueNotifyingHost(midiToggle.getToggleState());
    };

    addAndMakeVisible(envelope);
    envelope.setAdsrMode(true);
    envelope.setEnv(audioProcessor.adsrEnv);
    envelope.addListener(&audioProcessor);

    audioProcessor.attack->addListener(this);
    audioProcessor.decay->addListener(this);
    audioProcessor.sustain->addListener(this);
    audioProcessor.release->addListener(this);
}

MidiComponent::~MidiComponent() {
    envelope.removeListener(&audioProcessor);
    audioProcessor.attack->removeListener(this);
    audioProcessor.decay->removeListener(this);
    audioProcessor.sustain->removeListener(this);
    audioProcessor.release->removeListener(this);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    DBG("MidiComponent::handleAsyncUpdate");
    Env newEnv = Env::adsr(
        audioProcessor.attack->getValueUnnormalised(),
        audioProcessor.decay->getValueUnnormalised(),
        audioProcessor.sustain->getValueUnnormalised(),
        audioProcessor.release->getValueUnnormalised(),
        1.0,
        std::vector<EnvCurve>{ audioProcessor.attackShape->getValueUnnormalised(), audioProcessor.decayShape->getValueUnnormalised(), audioProcessor.releaseShape->getValueUnnormalised() }
    );

    envelope.setEnv(newEnv);
}

void MidiComponent::resized() {
    auto area = getLocalBounds().reduced(5);
    midiToggle.setBounds(area.removeFromTop(50));
    envelope.setBounds(area.removeFromTop(200));
    keyboard.setBounds(area.removeFromBottom(100));
}

void MidiComponent::paint(juce::Graphics& g) {
    auto rect = getLocalBounds().reduced(5);
    g.setColour(getLookAndFeel().findColour(groupComponentBackgroundColourId));
    g.fillRect(rect);
}
