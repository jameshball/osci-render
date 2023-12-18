#include "MidiComponent.h"
#include "PluginEditor.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    setText("MIDI Settings");

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
    envelope.setGrid(EnvelopeComponent::GridBoth, EnvelopeComponent::GridNone, 0.1, 0.25);

    audioProcessor.attackTime->addListener(this);
    audioProcessor.attackLevel->addListener(this);
    audioProcessor.attackShape->addListener(this);
    audioProcessor.decayTime->addListener(this);
    audioProcessor.decayShape->addListener(this);
    audioProcessor.sustainLevel->addListener(this);
    audioProcessor.releaseTime->addListener(this);
    audioProcessor.releaseShape->addListener(this);
}

MidiComponent::~MidiComponent() {
    envelope.removeListener(&audioProcessor);
    audioProcessor.attackTime->removeListener(this);
    audioProcessor.attackLevel->removeListener(this);
    audioProcessor.attackShape->removeListener(this);
    audioProcessor.decayTime->removeListener(this);
    audioProcessor.decayShape->removeListener(this);
    audioProcessor.sustainLevel->removeListener(this);
    audioProcessor.releaseTime->removeListener(this);
    audioProcessor.releaseShape->removeListener(this);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    Env newEnv = Env(
        { 
            0.0,
            audioProcessor.attackLevel->getValueUnnormalised(),
            audioProcessor.sustainLevel->getValueUnnormalised(),
            0.0
        },
        {
            audioProcessor.attackTime->getValueUnnormalised(),
            audioProcessor.decayTime->getValueUnnormalised(),
            audioProcessor.releaseTime->getValueUnnormalised()
        },
        std::vector<EnvCurve>{ audioProcessor.attackShape->getValueUnnormalised(), audioProcessor.decayShape->getValueUnnormalised(), audioProcessor.releaseShape->getValueUnnormalised() },
        2
    );

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        audioProcessor.adsrEnv = newEnv;
    }

    envelope.setEnv(newEnv);
}

void MidiComponent::resized() {
    auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
    midiToggle.setBounds(area.removeFromTop(30));
    area.removeFromTop(5);
    keyboard.setBounds(area.removeFromBottom(50));
    envelope.setBounds(area);
}

void MidiComponent::paint(juce::Graphics& g) {
    juce::GroupComponent::paint(g);
}
